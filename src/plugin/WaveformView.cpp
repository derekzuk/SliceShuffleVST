#include "WaveformView.h"
#include <algorithm>
#include <vector>

namespace {
constexpr const char* kSliceShuffleExportDragType = "sliceshuffle-export";
} // namespace

WaveformView::WaveformView(SliceShufflePluginProcessor& proc) : processor_(proc)
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

  // Build segments: when shortened (after deletes) use logical window = exactly Window Size slots.
  const size_t nPos = state->playbackOrder.size();
  const bool useLogicalWindow = (nPos > 0 && nPos != state->slices.size());
  std::vector<WindowSegment> segments;
  if (useLogicalWindow)
  {
    const auto [startLogical, endLogical] = processor_.getWindowLogicalPositionRange(*state);
    segments = buildWindowSegmentsFromLogicalRange(*state, startLogical, endLogical);
  }
  else if (rangeLength > 0)
    segments = buildWindowSegments(*state, rangeStart, rangeEnd);
  size_t totalWindowSamples = 0;
  for (const auto& s : segments)
    totalWindowSamples += s.length;

  // Selected slices: reddish highlight (by logical position in playback order).
  if (!state->slices.empty() && totalWindowSamples > 0)
  {
    g.setColour(juce::Colour(0x44cc3333)); // semi-transparent red
    const float totalF = static_cast<float>(totalWindowSamples);
    for (size_t logicalIdx : selectedSliceIndices_)
    {
      for (const auto& s : segments)
      {
        if (s.logicalIndex != logicalIdx) continue;
        const float x0 = (static_cast<float>(s.startOffset) / totalF) * width;
        const float x1 = (static_cast<float>(s.startOffset + s.length) / totalF) * width;
        g.fillRect(x0, 0.0f, std::max(1.0f, x1 - x0), height);
        break;
      }
    }
  }

  // Slice grid overlay: use the same segments so lines stay within slice boundaries.
  if (totalWindowSamples > 0)
  {
    g.setColour(juce::Colour(0x88ffffff));
    const float totalF = static_cast<float>(totalWindowSamples);
    g.drawVerticalLine(0, 0.0f, height); // left boundary of first segment
    for (const auto& s : segments)
    {
      const float x = (static_cast<float>(s.startOffset + s.length) / totalF) * width;
      const int xi = static_cast<int>(x);
      if (xi > 0 && xi < static_cast<int>(width))
        g.drawVerticalLine(xi, 0.0f, height);
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
  juce::int64 rangeStart;
  juce::int64 rangeEnd;
  if (overrideStart_ >= 0 && overrideEnd_ > overrideStart_)
  {
    rangeStart = overrideStart_;
    rangeEnd = overrideEnd_;
  }
  else
  {
    auto r = processor_.getWindowRangeSnappedToSlices(*state);
    rangeStart = r.first;
    rangeEnd = r.second;
  }
  const juce::int64 rangeLength = juce::jmax(juce::int64(0), rangeEnd - rangeStart);
  std::vector<WindowSegment> segments;
  const size_t nPos = state->playbackOrder.size();
  if (nPos > 0 && nPos != state->slices.size())
  {
    const auto [startLogical, endLogical] = processor_.getWindowLogicalPositionRange(*state);
    segments = buildWindowSegmentsFromLogicalRange(*state, startLogical, endLogical);
  }
  else if (rangeLength > 0)
    segments = buildWindowSegments(*state, rangeStart, rangeEnd);
  if (segments.empty()) return -1;
  size_t totalSamples = 0;
  for (const auto& s : segments)
    totalSamples += s.length;
  if (totalSamples == 0) return -1;
  const size_t offset = static_cast<size_t>((x / width) * static_cast<float>(totalSamples));
  for (const auto& s : segments)
  {
    if (offset < s.startOffset + s.length)
      return static_cast<int>(s.logicalIndex);
  }
  return static_cast<int>(segments.back().logicalIndex);
}

std::unordered_set<size_t> WaveformView::sliceIndicesInXRange(float x0, float x1) const
{
  std::unordered_set<size_t> out;
  auto state = processor_.getPreparedState();
  if (!state || state->slices.empty() || state->lengthInSamples <= 0)
    return out;
  const float width = static_cast<float>(getWidth());
  if (width <= 0.0f) return out;
  juce::int64 rangeStart;
  juce::int64 rangeEnd;
  if (overrideStart_ >= 0 && overrideEnd_ > overrideStart_)
  {
    rangeStart = overrideStart_;
    rangeEnd = overrideEnd_;
  }
  else
  {
    auto r = processor_.getWindowRangeSnappedToSlices(*state);
    rangeStart = r.first;
    rangeEnd = r.second;
  }
  const juce::int64 rangeLength = juce::jmax(juce::int64(0), rangeEnd - rangeStart);
  std::vector<WindowSegment> segments;
  const size_t nPos = state->playbackOrder.size();
  if (nPos > 0 && nPos != state->slices.size())
  {
    const auto [startLogical, endLogical] = processor_.getWindowLogicalPositionRange(*state);
    segments = buildWindowSegmentsFromLogicalRange(*state, startLogical, endLogical);
  }
  else if (rangeLength > 0)
    segments = buildWindowSegments(*state, rangeStart, rangeEnd);
  if (segments.empty()) return out;
  size_t totalSamples = 0;
  for (const auto& s : segments)
    totalSamples += s.length;
  if (totalSamples == 0) return out;
  const float xLo = juce::jmin(x0, x1);
  const float xHi = juce::jmax(x0, x1);
  const size_t offsetLo = static_cast<size_t>((xLo / width) * static_cast<float>(totalSamples));
  const size_t offsetHi = static_cast<size_t>((xHi / width) * static_cast<float>(totalSamples));
  for (const auto& s : segments)
  {
    const size_t segEnd = s.startOffset + s.length;
    if (segEnd > offsetLo && s.startOffset < offsetHi)
      out.insert(s.logicalIndex);
  }
  return out;
}

std::vector<WaveformView::WindowSegment> WaveformView::buildWindowSegments(
    const PreparedState& state, juce::int64 rangeStart, juce::int64 rangeEnd) const
{
  std::vector<WindowSegment> segments;
  const size_t n = state.playbackOrder.size();
  if (n == 0 || state.slices.empty()) return segments;
  size_t offset = 0;
  for (size_t logical = 0; logical < n; ++logical)
  {
    const size_t phys = state.playbackOrder[logical];
    if (phys >= state.slices.size()) continue;
    const auto& sl = state.slices[phys];
    const juce::int64 sliceStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 sliceEnd = sliceStart + static_cast<juce::int64>(sl.lengthSamples);
    const juce::int64 overlapStart = juce::jmax(sliceStart, rangeStart);
    const juce::int64 overlapEnd = juce::jmin(sliceEnd, rangeEnd);
    if (overlapEnd <= overlapStart) continue;
    const size_t len = static_cast<size_t>(overlapEnd - overlapStart);
    segments.push_back({logical, offset, len});
    offset += len;
  }
  return segments;
}

std::vector<WaveformView::WindowSegment> WaveformView::buildWindowSegmentsFromLogicalRange(
    const PreparedState& state, size_t startLogical, size_t endLogical) const
{
  std::vector<WindowSegment> segments;
  const size_t n = state.playbackOrder.size();
  if (n == 0 || state.slices.empty() || startLogical > endLogical || endLogical >= n)
    return segments;
  size_t offset = 0;
  for (size_t logical = startLogical; logical <= endLogical; ++logical)
  {
    const size_t phys = state.playbackOrder[logical];
    if (phys >= state.slices.size()) continue;
    const size_t len = state.slices[phys].lengthSamples;
    segments.push_back({logical, offset, len});
    offset += len;
  }
  return segments;
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
  dragStartPos_ = e.getPosition();
  dragStarted_ = false;
  shiftDragActive_ = false;

  if (e.mods.isShiftDown())
  {
    // Don't toggle here; we'll either do shift-drag selection in mouseDrag or single-toggle in mouseUp
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
  if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
  {
    if (!selectedSliceIndices_.empty())
    {
      processor_.removeSelectedSlices(selectedSliceIndices_);
      clearSelection();
      return true;
    }
    return false;
  }
  if (selectedSliceIndices_.empty())
    return false;
  // Left / Right: move selected slice(s) in playback order; highlight follows the moved slice
  const int direction = key.getKeyCode() == juce::KeyPress::rightKey ? 1
                        : key.getKeyCode() == juce::KeyPress::leftKey ? -1
                                                                      : 0;
  if (direction == 0)
    return false;
  auto state = processor_.getPreparedState();
  const size_t numSlices = state ? state->playbackOrder.size() : static_cast<size_t>(processor_.getNumSlices());
  size_t windowLeft = 0;
  size_t windowRight = numSlices > 0 ? numSlices - 1 : 0;
  if (state)
  {
    const auto [wLeft, wRight] = processor_.getWindowLogicalPositionRange(*state);
    windowLeft = wLeft;
    windowRight = wRight;
  }
  // Don't move left if the leftmost selected slice is at the left edge of the window
  if (direction < 0)
  {
    const size_t leftmost = *std::min_element(
        selectedSliceIndices_.begin(), selectedSliceIndices_.end());
    if (leftmost <= windowLeft)
      return true; // consume key, no move
  }
  // Don't move right if the rightmost selected slice is at the right edge of the window
  if (direction > 0)
  {
    const size_t rightmost = *std::max_element(
        selectedSliceIndices_.begin(), selectedSliceIndices_.end());
    if (rightmost >= windowRight)
      return true; // consume key, no move
  }
  processor_.moveSelectedSlicesInOrder(selectedSliceIndices_, direction);

  // Highlight follows the slice; at left/right boundary keep selection (same as other side)
  std::unordered_set<size_t> newSel;
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
  if (e.mods.isShiftDown())
  {
    if (e.getDistanceFromDragStart() >= kDragStartThresholdPx)
    {
      shiftDragActive_ = true;
      const float x0 = static_cast<float>(dragStartPos_.getX());
      const float x1 = static_cast<float>(e.getPosition().getX());
      selectedSliceIndices_ = sliceIndicesInXRange(x0, x1);
      repaint();
    }
    return;
  }
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
  container->startDragging(juce::var(kSliceShuffleExportDragType), this, juce::ScaledImage(), true);
}

void WaveformView::mouseUp(const juce::MouseEvent& e)
{
  if (e.mods.isShiftDown() && !shiftDragActive_)
  {
    const int idx = sliceIndexAt(static_cast<float>(e.getPosition().getX()));
    if (idx >= 0)
    {
      const size_t u = static_cast<size_t>(idx);
      if (selectedSliceIndices_.count(u))
        selectedSliceIndices_.erase(u);
      else
        selectedSliceIndices_.insert(u);
      // Ensure arrow keys work immediately after any click selection.
      grabKeyboardFocus();
      repaint();
    }
  }
  else if (!shiftDragActive_ && !selectedSliceIndices_.empty())
  {
    const int idx = sliceIndexAt(static_cast<float>(e.getPosition().getX()));
    if (idx >= 0)
    {
      const size_t u = static_cast<size_t>(idx);
      if (selectedSliceIndices_.count(u) == 0)
      {
        selectedSliceIndices_.clear();
        repaint();
      }
    }
  }
  shiftDragActive_ = false;
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
