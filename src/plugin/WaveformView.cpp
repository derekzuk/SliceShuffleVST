#include "WaveformView.h"
#include <vector>

namespace {
constexpr const char* kCutShuffleExportDragType = "cutshuffle-export";
} // namespace

WaveformView::WaveformView(CutShufflePluginProcessor& proc) : processor_(proc)
{
  formatManager_.registerBasicFormats();
  thumbnail_.addChangeListener(this);
  setOpaque(true);
  setWantsKeyboardFocus(true);
}

void WaveformView::paint(juce::Graphics& g)
{
  g.fillAll(juce::Colour(0xff1a1a1a));

  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
  {
    g.setColour(juce::Colours::grey);
    g.setFont(14.0f);
    g.drawText("Load a sample to see waveform", getLocalBounds(), juce::Justification::centred);
    return;
  }

  const auto bounds = getLocalBounds().toFloat();
  const float width = bounds.getWidth();
  const float height = bounds.getHeight();
  const juce::int64 totalSamples = state->lengthInSamples;
  juce::int64 rangeStart;
  juce::int64 rangeEnd;
  bool useOverride = (overrideStart_ >= 0 && overrideEnd_ > overrideStart_);
  if (useOverride)
  {
    rangeStart = juce::jlimit(juce::int64(0), totalSamples, overrideStart_);
    rangeEnd = juce::jlimit(juce::int64(0), totalSamples, overrideEnd_);
    if (rangeEnd <= rangeStart)
      rangeEnd = juce::jmin(totalSamples, rangeStart + 1);
  }
  else
  {
    auto [windowStartSample, windowEndSample] = processor_.getWindowRangeSnappedToSlices(*state);
    rangeStart = juce::jlimit(juce::int64(0), totalSamples, windowStartSample);
    rangeEnd = juce::jmin(totalSamples, windowEndSample);
  }
  const juce::int64 rangeLength = juce::jmax(juce::int64(0), rangeEnd - rangeStart);

  // Build a window buffer: when overriding (live drag) use playback order for that range; otherwise use window.
  juce::AudioBuffer<float> windowBuffer;
  if (useOverride && rangeLength > 0 && rangeStart + rangeLength <= totalSamples)
    processor_.renderSampleRangeToBuffer(*state, rangeStart, rangeEnd, windowBuffer);
  else if (!useOverride)
    processor_.renderWindowToBuffer(*state, windowBuffer);

  const int numChannels = windowBuffer.getNumChannels();
  const int numSamples = windowBuffer.getNumSamples();
  if (numSamples > 0 && rangeLength > 0)
  {
    g.setColour(juce::Colour(0xff404060));
    const float scale = height * 0.45f;
    const float midY = height * 0.5f;
    const int w = static_cast<int>(width);
    std::vector<float> maxPerCol(static_cast<size_t>(w), -1.0f);
    std::vector<float> minPerCol(static_cast<size_t>(w), 1.0f);
    const float rangeLenF = static_cast<float>(numSamples);
    for (int x = 0; x < w; ++x)
    {
      const int sampleLo = static_cast<int>((static_cast<float>(x) / width) * rangeLenF);
      const int sampleHi = static_cast<int>((static_cast<float>(x + 1) / width) * rangeLenF) + 1;
      const int lo = juce::jlimit(0, numSamples, sampleLo);
      const int hi = juce::jlimit(0, numSamples, sampleHi);
      if (hi <= lo) continue;
      float mx = -1.0f;
      float mn = 1.0f;
      for (int ch = 0; ch < numChannels; ++ch)
      {
        const float* data = windowBuffer.getReadPointer(ch);
        for (int i = lo; i < hi; ++i)
        {
          const float s = data[i];
          if (s > mx) mx = s;
          if (s < mn) mn = s;
        }
      }
      maxPerCol[static_cast<size_t>(x)] = mx;
      minPerCol[static_cast<size_t>(x)] = mn;
    }
    juce::Path p;
    p.startNewSubPath(0.0f, midY);
    for (int x = 0; x < w; ++x)
      p.lineTo(static_cast<float>(x), juce::jlimit(0.0f, height, midY - maxPerCol[static_cast<size_t>(x)] * scale));
    for (int x = w - 1; x >= 0; --x)
      p.lineTo(static_cast<float>(x), juce::jlimit(0.0f, height, midY - minPerCol[static_cast<size_t>(x)] * scale));
    p.closeSubPath();
    g.fillPath(p);
  }

  // Selected slices: reddish highlight (positions relative to window range).
  // For now this still uses physical slice positions; selection logic remains unchanged.
  if (!state->slices.empty() && rangeLength > 0)
  {
    g.setColour(juce::Colour(0x44cc3333)); // semi-transparent red
    for (size_t idx : selectedSliceIndices_)
    {
      if (idx >= state->slices.size()) continue;
      const auto& slice = state->slices[idx];
      const juce::int64 sliceStart = static_cast<juce::int64>(slice.startSample);
      const juce::int64 sliceEnd = sliceStart + static_cast<juce::int64>(slice.lengthSamples);
      if (sliceEnd <= rangeStart || sliceStart >= rangeEnd) continue;
      const float x0 = (static_cast<float>(juce::jmax(sliceStart, rangeStart) - rangeStart) / static_cast<float>(rangeLength)) * width;
      const float x1 = (static_cast<float>(juce::jmin(sliceEnd, rangeEnd) - rangeStart) / static_cast<float>(rangeLength)) * width;
      g.fillRect(x0, 0.0f, std::max(1.0f, x1 - x0), height);
    }
  }

  // Slice grid overlay (only slices inside window)
  g.setColour(juce::Colour(0x88ffffff));
  for (const auto& slice : state->slices)
  {
    const juce::int64 sliceStart = static_cast<juce::int64>(slice.startSample);
    if (sliceStart < rangeStart || sliceStart >= rangeEnd) continue;
    const float x = (static_cast<float>(sliceStart - rangeStart) / static_cast<float>(rangeLength)) * width;
    g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
  }
  if (!state->slices.empty())
  {
    const auto& last = state->slices.back();
    const juce::int64 edge = static_cast<juce::int64>(last.startSample + last.lengthSamples);
    if (edge > rangeStart && edge <= rangeEnd)
    {
      const float x = (static_cast<float>(edge - rangeStart) / static_cast<float>(rangeLength)) * width;
      g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
    }
  }

  // Window edges (this view shows the window; subtle left/right border)
  if (rangeEnd > rangeStart && totalSamples > 0)
  {
    g.setColour(juce::Colour(0x400080ff));
    g.drawVerticalLine(0, 0.0f, height);
    g.drawVerticalLine(static_cast<int>(width), 0.0f, height);
  }
}

void WaveformView::resized() {}

int WaveformView::sliceIndexAt(float x) const
{
  auto state = processor_.getPreparedState();
  if (!state || state->slices.empty() || state->lengthInSamples <= 0)
    return -1;
  const float width = static_cast<float>(getWidth());
  if (width <= 0.0f) return -1;
  auto [rangeStart, rangeEnd] = processor_.getWindowRangeSnappedToSlices(*state);
  const juce::int64 rangeLength = juce::jmax(juce::int64(0), rangeEnd - rangeStart);
  if (rangeLength <= 0) return -1;
  const juce::int64 sample = rangeStart + static_cast<juce::int64>((x / width) * static_cast<float>(rangeLength));
  for (size_t i = 0; i < state->slices.size(); ++i)
  {
    const auto& sl = state->slices[i];
    const juce::int64 sliceStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 sliceEnd = sliceStart + static_cast<juce::int64>(sl.lengthSamples);
    if (sample >= sliceStart && sample < sliceEnd)
      return static_cast<int>(i);
  }
  return -1;
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
  dragStartPos_ = e.getPosition();
  dragStarted_ = false;

  if (e.mods.isCtrlDown() || e.mods.isCommandDown())
  {
    const int idx = sliceIndexAt(static_cast<float>(e.getPosition().getX()));
    if (idx >= 0)
    {
      const size_t u = static_cast<size_t>(idx);
      if (selectedSliceIndices_.count(u))
        selectedSliceIndices_.erase(u);
      else
        selectedSliceIndices_.insert(u);
      repaint();
    }
  }
  else
    grabKeyboardFocus();
}

bool WaveformView::keyPressed(const juce::KeyPress& key)
{
  if (key == juce::KeyPress::spaceKey)
  {
    if (processor_.isPreviewActive())
      processor_.stopPreview();
    else
      processor_.startPreview();
    return true;
  }
  if (selectedSliceIndices_.empty())
    return false;
  // Shift+Left / Shift+Right: swap selected slice(s) with neighbour; highlight follows the moved slice
  if (!key.getModifiers().isShiftDown())
    return false;
  const int direction = key.getKeyCode() == juce::KeyPress::rightKey ? 1
                        : key.getKeyCode() == juce::KeyPress::leftKey ? -1
                                                                      : 0;
  if (direction == 0)
    return false;
  processor_.moveSelectedSlicesInOrder(selectedSliceIndices_, direction);

  // Highlight follows the slice; at left/right boundary keep selection (same as other side)
  std::unordered_set<size_t> newSel;
  const size_t numSlices = static_cast<size_t>(processor_.getNumSlices());
  for (size_t p : selectedSliceIndices_)
  {
    const size_t nxt = (direction > 0) ? p + 1 : (p > 0 ? p - 1 : p);
    if (nxt < numSlices)
      newSel.insert(nxt);
    else
      newSel.insert(p); // at right edge (move right) or left edge (move left), keep selection
  }
  selectedSliceIndices_ = std::move(newSel);

  repaint();
  return true;
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
  if (dragStarted_)
    return;
  if (e.getDistanceFromDragStart() < kDragStartThresholdPx)
    return;
  auto state = processor_.getPreparedState();
  if (!state || state->buffer.getNumSamples() == 0)
    return;
  auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
  if (!container)
    return;
  dragStarted_ = true;
  container->startDragging(juce::var(kCutShuffleExportDragType), this, juce::ScaledImage(), true);
}

void WaveformView::changeListenerCallback(juce::ChangeBroadcaster*)
{
  repaint();
}

void WaveformView::refresh()
{
  juce::File newFile(processor_.getLoadedSamplePath());
  if (newFile != currentFile_)
  {
    currentFile_ = newFile;
    selectedSliceIndices_.clear(); // clear selection when sample changes
    if (currentFile_.existsAsFile())
      thumbnail_.setSource(new juce::FileInputSource(currentFile_));
    else
      thumbnail_.clear();
  }
  // Prune selection if slice count changed (e.g. BPM/granularity)
  if (auto state = processor_.getPreparedState())
  {
    for (auto it = selectedSliceIndices_.begin(); it != selectedSliceIndices_.end();)
    {
      if (*it >= state->slices.size())
        it = selectedSliceIndices_.erase(it);
      else
        ++it;
    }
  }
  repaint();
}

void WaveformView::setDisplayWindowOverride(juce::int64 startSample, juce::int64 endSample)
{
  if (startSample < 0 || endSample <= startSample)
  {
    if (overrideStart_ >= 0)
    {
      overrideStart_ = -1;
      overrideEnd_ = -1;
      repaint();
    }
    return;
  }
  if (overrideStart_ != startSample || overrideEnd_ != endSample)
  {
    overrideStart_ = startSample;
    overrideEnd_ = endSample;
    repaint();
  }
}
