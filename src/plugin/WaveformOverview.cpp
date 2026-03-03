#include "WaveformOverview.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace {
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
} // namespace

WaveformOverview::WaveformOverview(SliceShufflePluginProcessor& proc) : processor_(proc)
{
  setOpaque(true);
}

void WaveformOverview::resized()
{
  ensureEnvelopeBuilt();
}

void WaveformOverview::ensureEnvelopeBuilt()
{
  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
  {
    envelopeReady_.store(false);
    return;
  }
  const int w = getWidth();
  if (w <= 0)
    return;
  const juce::int64 len = state->lengthInSamples;
  if (!envelopeReady_.load() || cachedWidth_ != w || cachedLengthInSamples_ != len)
    rebuildEnvelopeAsync();
}

float WaveformOverview::sampleToX(juce::int64 sample, juce::int64 totalSamples, float width) const
{
  if (totalSamples <= 0)
    return 0.0f;
  return width * (static_cast<float>(sample) / static_cast<float>(totalSamples));
}

juce::int64 WaveformOverview::xToSample(float x, juce::int64 totalSamples, float width) const
{
  if (width <= 0.0f)
    return 0;
  float t = (x / width);
  t = juce::jlimit(0.0f, 1.0f, t);
  return static_cast<juce::int64>(t * static_cast<float>(totalSamples));
}

int WaveformOverview::hitTestWindow(float x, float windowStartX, float windowEndX) const
{
  const float margin = kEdgeHitWidthPx * 1.5f;
  if (x < windowStartX - margin)
    return -2;
  if (x <= windowEndX + margin)
    return 0; // entire window is draggable (no edge resize)
  return -2;
}

void WaveformOverview::setWindowFromSliceRange(size_t startSliceIdx, size_t endSliceIdxExclusive)
{
  (void)endSliceIdxExclusive; // used only for edge-snap path; position/size set below
  auto state = processor_.getPreparedState();
  if (!state || state->slices.empty())
    return;
  const size_t numSlices = state->slices.size();
  juce::AudioProcessorValueTreeState& apvts = processor_.getValueTreeState();

  // Keep the current Window # (slices) unchanged; only update position.
  const float rawWindow = static_cast<float>(apvts.getRawParameterValue(kWindowBeatsId)->load());
  int windowSlices = juce::jlimit(kMinWindowSlices, kMaxWindowSlices, static_cast<int>(std::round(rawWindow)));
  windowSlices = (windowSlices + 1) & ~1; // even: 4, 6, 8, ...

  const size_t maxStart = numSlices >= static_cast<size_t>(windowSlices)
                              ? numSlices - static_cast<size_t>(windowSlices)
                              : 0;
  startSliceIdx = juce::jmin(startSliceIdx, maxStart);
  const float posNorm = (maxStart > 0) ? (static_cast<float>(startSliceIdx) / static_cast<float>(maxStart)) : 0.0f;

  if (auto* param = apvts.getParameter(kWindowPositionId))
    param->setValueNotifyingHost(posNorm);
  else
    apvts.getParameterAsValue(kWindowPositionId).setValue(static_cast<double>(posNorm));
}

juce::int64 WaveformOverview::nearestSliceBoundarySample(juce::int64 sample) const
{
  if (sliceEnds_.empty())
    return 0;
  const juce::int64 leftBound = 0;
  const juce::int64 rightBound = sliceEnds_.back();
  if (sample <= leftBound)
    return leftBound;
  if (sample >= rightBound)
    return rightBound;
  auto it = std::lower_bound(sliceEnds_.begin(), sliceEnds_.end(), sample);
  const size_t idx = static_cast<size_t>(std::distance(sliceEnds_.begin(), it));
  const juce::int64 boundaryAfter = (idx < sliceEnds_.size()) ? static_cast<juce::int64>(sliceEnds_[idx]) : rightBound;
  const juce::int64 boundaryBefore = (idx > 0) ? static_cast<juce::int64>(sliceEnds_[idx - 1]) : leftBound;
  return (sample - boundaryBefore <= boundaryAfter - sample) ? boundaryBefore : boundaryAfter;
}

size_t WaveformOverview::boundaryToStartSliceIndex(juce::int64 boundarySample) const
{
  if (sliceEnds_.empty())
    return 0;
  if (boundarySample <= 0)
    return 0;
  for (size_t i = 0; i < sliceEnds_.size(); ++i)
    if (static_cast<juce::int64>(sliceEnds_[i]) == boundarySample)
      return i + 1;
  return sampleToSliceIndexBinary(boundarySample) + 1;
}

size_t WaveformOverview::boundaryToEndSliceIndexExclusive(juce::int64 boundarySample) const
{
  if (sliceEnds_.empty())
    return 0;
  for (size_t i = 0; i < sliceEnds_.size(); ++i)
    if (static_cast<juce::int64>(sliceEnds_[i]) == boundarySample)
      return i + 1;
  return juce::jmin(sliceEnds_.size(), sampleToSliceIndexBinary(boundarySample) + 1);
}

void WaveformOverview::rebuildEnvelopeAsync()
{
  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
    return;
  const int w = getWidth();
  if (w <= 0)
    return;

  const uint64_t jobId = ++envelopeJobId_;
  const juce::int64 totalSamples = state->lengthInSamples;
  const int numChannels = state->buffer.getNumChannels();
  const int numSamples = state->buffer.getNumSamples();
  juce::AudioBuffer<float> bufferCopy(numChannels, numSamples);
  for (int ch = 0; ch < numChannels; ++ch)
    bufferCopy.copyFrom(ch, 0, state->buffer, ch, 0, numSamples);

  envelopePool_.addJob([this, jobId, w, totalSamples, numChannels, numSamples, bufferCopy]() mutable
  {
    std::vector<float> maxPerCol(static_cast<size_t>(w), -1.0f);
    std::vector<float> minPerCol(static_cast<size_t>(w), 1.0f);
    const float width = static_cast<float>(w);
    for (int x = 0; x < w; ++x)
    {
      const int lo = static_cast<int>((static_cast<float>(x) / width) * numSamples);
      const int hi = juce::jmin(numSamples,
                                static_cast<int>((static_cast<float>(x + 1) / width) * numSamples) + 1);
      if (hi <= lo)
        continue;
      float mx = -1.0f;
      float mn = 1.0f;
      for (int ch = 0; ch < numChannels; ++ch)
      {
        const float* data = bufferCopy.getReadPointer(ch);
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

    juce::MessageManager::callAsync([this, jobId, w, totalSamples,
                                     maxOut = std::move(maxPerCol),
                                     minOut = std::move(minPerCol)]()
    {
      if (jobId != envelopeJobId_.load())
        return;
      maxPerCol_ = std::move(maxOut);
      minPerCol_ = std::move(minOut);
      cachedWidth_ = w;
      cachedLengthInSamples_ = totalSamples;
      envelopeReady_.store(true);
      repaint();
    });
  });
}

size_t WaveformOverview::sampleToSliceIndexBinary(juce::int64 sample) const
{
  if (sliceEnds_.empty())
    return 0;
  auto it = std::upper_bound(sliceEnds_.begin(), sliceEnds_.end(), sample);
  if (it == sliceEnds_.begin())
    return 0;
  if (it == sliceEnds_.end())
    return sliceEnds_.size() - 1;
  return static_cast<size_t>(std::distance(sliceEnds_.begin(), it) - 1);
}

void WaveformOverview::paint(juce::Graphics& g)
{
  g.fillAll(juce::Colour(0xff252530));

  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
  {
    g.setColour(juce::Colours::grey);
    g.setFont(11.0f);
    g.drawText("Load a sample", getLocalBounds(), juce::Justification::centred);
    return;
  }

  const auto bounds = getLocalBounds().toFloat();
  const float width = bounds.getWidth();
  const float height = bounds.getHeight();
  const juce::int64 totalSamples = state->lengthInSamples;

  if (!envelopeReady_.load() || maxPerCol_.empty() || static_cast<int>(maxPerCol_.size()) != getWidth())
  {
    g.setColour(juce::Colours::grey);
    g.setFont(11.0f);
    g.drawText("Loading…", getLocalBounds(), juce::Justification::centred);
    // Still draw window overlay if we have drag state
    if (dragWindowStartSample_ >= 0 && dragWindowEndSample_ > dragWindowStartSample_ && totalSamples > 0)
    {
      const float startX = sampleToX(dragWindowStartSample_, totalSamples, width);
      const float endX = sampleToX(dragWindowEndSample_, totalSamples, width);
      const float ww = std::max(2.0f, endX - startX);
      g.setColour(juce::Colour(0x3800aaff));
      g.fillRect(startX, 0.0f, ww, height);
      g.setColour(juce::Colour(0xc00080ff));
      g.drawVerticalLine(static_cast<int>(startX), 0.0f, height);
      g.drawVerticalLine(static_cast<int>(endX), 0.0f, height);
    }
    return;
  }

  const float scale = height * 0.45f;
  const float midY = height * 0.5f;
  const int w = static_cast<int>(width);

  juce::Path p;
  p.startNewSubPath(0.0f, midY);
  for (int x = 0; x < w; ++x)
    p.lineTo(static_cast<float>(x),
             juce::jlimit(0.0f, height, midY - maxPerCol_[static_cast<size_t>(x)] * scale));
  for (int x = w - 1; x >= 0; --x)
    p.lineTo(static_cast<float>(x),
             juce::jlimit(0.0f, height, midY - minPerCol_[static_cast<size_t>(x)] * scale));
  p.closeSubPath();
  g.setColour(juce::Colour(0xff404060));
  g.fillPath(p);

  // Window overlay: during drag use local sample range; otherwise use APVTS (snapped)
  juce::int64 startSample = 0;
  juce::int64 endSample = 0;
  if (dragWindowStartSample_ >= 0 && dragWindowEndSample_ > dragWindowStartSample_)
  {
    startSample = dragWindowStartSample_;
    endSample = dragWindowEndSample_;
  }
  else
  {
    auto range = processor_.getWindowRangeSnappedToSlices();
    startSample = range.first;
    endSample = range.second;
  }
  if (endSample > startSample && totalSamples > 0)
  {
    const float startX = sampleToX(startSample, totalSamples, width);
    const float endX = sampleToX(endSample, totalSamples, width);
    const float ww = std::max(2.0f, endX - startX);
    g.setColour(juce::Colour(0x3800aaff));
    g.fillRect(startX, 0.0f, ww, height);
    g.setColour(juce::Colour(0xc00080ff));
    g.drawVerticalLine(static_cast<int>(startX), 0.0f, height);
    g.drawVerticalLine(static_cast<int>(endX), 0.0f, height);
  }
}

void WaveformOverview::mouseDown(const juce::MouseEvent& e)
{
  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
    return;

  const float width = static_cast<float>(getWidth());
  const juce::int64 totalSamples = state->lengthInSamples;
  auto [startSample, endSample] = processor_.getWindowRangeSnappedToSlices();
  const float startX = sampleToX(startSample, totalSamples, width);
  const float endX = sampleToX(endSample, totalSamples, width);

  const float x = static_cast<float>(e.getPosition().getX());
  dragHit_ = hitTestWindow(x, startX, endX);
  dragStartX_ = x;
  dragStartSampleStart_ = startSample;
  dragStartSampleEnd_ = endSample;
  dragWindowStartSample_ = -1;
  dragWindowEndSample_ = -1;

  // Precompute slice end samples for binary search during drag
  sliceEnds_.clear();
  const std::vector<sliceshuffle::Slice>& slices = state->slices;
  sliceEnds_.reserve(slices.size());
  for (const auto& sl : slices)
    sliceEnds_.push_back(static_cast<juce::int64>(sl.startSample + sl.lengthSamples));

  if (dragHit_ == -2)
  {
    // Click in empty area: move window to this spot (center window on click)
    const juce::int64 windowLen = endSample - startSample;
    if (windowLen > 0 && !sliceEnds_.empty())
    {
      const juce::int64 centerSample = xToSample(x, totalSamples, width);
      const juce::int64 maxStart = totalSamples - windowLen;
      const juce::int64 newStart = juce::jlimit(juce::int64(0), juce::jmax(juce::int64(0), maxStart),
                                                centerSample - windowLen / 2);
      const size_t si = sampleToSliceIndexBinary(newStart);
      size_t ei = sampleToSliceIndexBinary(newStart + windowLen - 1);
      if (ei < si)
        ei = si;
      ei += 1;
      setWindowFromSliceRange(si, ei);
      auto newRange = processor_.getWindowRangeSnappedToSlices();
      dragStartSampleStart_ = newRange.first;
      dragStartSampleEnd_ = newRange.second;
      repaint();
    }
    e.source.enableUnboundedMouseMovement(true); // allow dragging window to cursor past bounds
    return;
  }

  if (dragHit_ != -2)
  {
    if (auto* param = processor_.getValueTreeState().getParameter(kWindowPositionId))
      param->beginChangeGesture();
    e.source.enableUnboundedMouseMovement(true);
  }
  else
    e.source.enableUnboundedMouseMovement(true); // allow dragging window to cursor past bounds
}

void WaveformOverview::mouseDrag(const juce::MouseEvent& e)
{
  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples <= 0)
    return;

  const float width = static_cast<float>(getWidth());
  const juce::int64 totalSamples = state->lengthInSamples;
  const float x = static_cast<float>(e.getPosition().getX());

  // Update local window in sample space (continuous, no snap); commit to APVTS only on mouseUp
  if (dragHit_ == 0)
  {
    const float dx = x - dragStartX_;
    const float samplePerPx = totalSamples / juce::jmax(1.0f, width);
    const juce::int64 deltaSample = static_cast<juce::int64>(dx * samplePerPx);
    juce::int64 newStart = dragStartSampleStart_ + deltaSample;
    juce::int64 newEnd = dragStartSampleEnd_ + deltaSample;
    newStart = juce::jlimit(juce::int64(0), totalSamples, newStart);
    newEnd = juce::jlimit(juce::int64(0), totalSamples, newEnd);
    if (newEnd <= newStart)
      newEnd = juce::jmin(totalSamples, newStart + (dragStartSampleEnd_ - dragStartSampleStart_));
    dragWindowStartSample_ = newStart;
    dragWindowEndSample_ = newEnd;
  }
  else
  {
    // Empty-area drag: move the window to follow the cursor (same size, center on cursor)
    const juce::int64 windowLen = dragStartSampleEnd_ - dragStartSampleStart_;
    if (windowLen <= 0)
      return;
    const juce::int64 centerSample = xToSample(x, totalSamples, width);
    const juce::int64 maxStart = totalSamples - windowLen;
    juce::int64 newStart = juce::jlimit(juce::int64(0), juce::jmax(juce::int64(0), maxStart),
                                       centerSample - windowLen / 2);
    dragWindowStartSample_ = newStart;
    dragWindowEndSample_ = newStart + windowLen;
  }

  if (liveWindowRangeCallback_)
    liveWindowRangeCallback_(dragWindowStartSample_, dragWindowEndSample_);

  repaint();
}

void WaveformOverview::mouseUp(const juce::MouseEvent& e)
{
  e.source.enableUnboundedMouseMovement(false);

  const bool hadDragHit = (dragHit_ != -2);

  if (hadDragHit)
  {
    auto state = processor_.getPreparedState();
    if (state && !state->slices.empty() && state->lengthInSamples > 0 &&
        dragWindowStartSample_ >= 0 && dragWindowEndSample_ > dragWindowStartSample_)
    {
      const juce::int64 totalSamples = state->lengthInSamples;
      const juce::int64 snappedLeft = nearestSliceBoundarySample(dragWindowStartSample_);
      const juce::int64 snappedRight = nearestSliceBoundarySample(dragWindowEndSample_);

      const float rawWindow = static_cast<float>(processor_.getValueTreeState().getRawParameterValue(kWindowBeatsId)->load());
      int windowSlices = juce::jlimit(kMinWindowSlices, kMaxWindowSlices, static_cast<int>(std::round(rawWindow)));
      windowSlices = (windowSlices + 1) & ~1;
      const size_t numSlices = state->slices.size();
      const size_t maxStart = numSlices >= static_cast<size_t>(windowSlices)
                                  ? numSlices - static_cast<size_t>(windowSlices)
                                  : 0;

      size_t startFromLeft = boundaryToStartSliceIndex(snappedLeft);
      startFromLeft = juce::jmin(startFromLeft, maxStart);
      const juce::int64 windowStartA = (startFromLeft == 0) ? juce::int64(0) : sliceEnds_[startFromLeft - 1];
      const juce::int64 windowEndA = (startFromLeft + static_cast<size_t>(windowSlices) <= sliceEnds_.size())
                                         ? sliceEnds_[startFromLeft + static_cast<size_t>(windowSlices) - 1]
                                         : totalSamples;
      const juce::int64 distA = std::abs(windowStartA - dragWindowStartSample_) + std::abs(windowEndA - dragWindowEndSample_);

      size_t endExclusiveFromRight = boundaryToEndSliceIndexExclusive(snappedRight);
      size_t startFromRight = (endExclusiveFromRight >= static_cast<size_t>(windowSlices))
                                  ? (endExclusiveFromRight - static_cast<size_t>(windowSlices))
                                  : 0;
      startFromRight = juce::jmin(startFromRight, maxStart);
      const juce::int64 windowStartB = (startFromRight == 0) ? juce::int64(0) : sliceEnds_[startFromRight - 1];
      const juce::int64 windowEndB = (startFromRight + static_cast<size_t>(windowSlices) <= sliceEnds_.size())
                                         ? sliceEnds_[startFromRight + static_cast<size_t>(windowSlices) - 1]
                                         : totalSamples;
      const juce::int64 distB = std::abs(windowStartB - dragWindowStartSample_) + std::abs(windowEndB - dragWindowEndSample_);

      const size_t startSliceIdx = (distA <= distB) ? startFromLeft : startFromRight;
      setWindowFromSliceRange(startSliceIdx, startSliceIdx + static_cast<size_t>(windowSlices));
    }

    if (auto* param = processor_.getValueTreeState().getParameter(kWindowPositionId))
      param->endChangeGesture();
  }
  else
  {
    // Dragged in empty area: snap to nearest boundaries and place window
    auto state = processor_.getPreparedState();
    if (state && !state->slices.empty() && state->lengthInSamples > 0 &&
        dragWindowStartSample_ >= 0 && dragWindowEndSample_ > dragWindowStartSample_)
    {
      const juce::int64 snappedLeft = nearestSliceBoundarySample(dragWindowStartSample_);
      const juce::int64 snappedRight = nearestSliceBoundarySample(dragWindowEndSample_);

      const float rawWindow = static_cast<float>(processor_.getValueTreeState().getRawParameterValue(kWindowBeatsId)->load());
      int windowSlices = juce::jlimit(kMinWindowSlices, kMaxWindowSlices, static_cast<int>(std::round(rawWindow)));
      windowSlices = (windowSlices + 1) & ~1;
      const size_t numSlices = state->slices.size();
      const size_t maxStart = numSlices >= static_cast<size_t>(windowSlices)
                                  ? numSlices - static_cast<size_t>(windowSlices)
                                  : 0;

      size_t startFromLeft = boundaryToStartSliceIndex(snappedLeft);
      startFromLeft = juce::jmin(startFromLeft, maxStart);
      const juce::int64 windowStartA = (startFromLeft == 0) ? juce::int64(0) : sliceEnds_[startFromLeft - 1];
      const juce::int64 windowEndA = (startFromLeft + static_cast<size_t>(windowSlices) <= sliceEnds_.size())
                                         ? sliceEnds_[startFromLeft + static_cast<size_t>(windowSlices) - 1]
                                         : state->lengthInSamples;
      const juce::int64 distA = std::abs(windowStartA - dragWindowStartSample_) + std::abs(windowEndA - dragWindowEndSample_);

      size_t endExclusiveFromRight = boundaryToEndSliceIndexExclusive(snappedRight);
      size_t startFromRight = (endExclusiveFromRight >= static_cast<size_t>(windowSlices))
                                  ? (endExclusiveFromRight - static_cast<size_t>(windowSlices))
                                  : 0;
      startFromRight = juce::jmin(startFromRight, maxStart);
      const juce::int64 windowStartB = (startFromRight == 0) ? juce::int64(0) : sliceEnds_[startFromRight - 1];
      const juce::int64 windowEndB = (startFromRight + static_cast<size_t>(windowSlices) <= sliceEnds_.size())
                                         ? sliceEnds_[startFromRight + static_cast<size_t>(windowSlices) - 1]
                                         : state->lengthInSamples;
      const juce::int64 distB = std::abs(windowStartB - dragWindowStartSample_) + std::abs(windowEndB - dragWindowEndSample_);

      const size_t startSliceIdx = (distA <= distB) ? startFromLeft : startFromRight;
      if (auto* param = processor_.getValueTreeState().getParameter(kWindowPositionId))
        param->beginChangeGesture();
      setWindowFromSliceRange(startSliceIdx, startSliceIdx + static_cast<size_t>(windowSlices));
      if (auto* p = processor_.getValueTreeState().getParameter(kWindowPositionId))
        p->endChangeGesture();
    }
  }

  dragHit_ = -2;
  dragWindowStartSample_ = -1;
  dragWindowEndSample_ = -1;

  if (liveWindowRangeCallback_)
    liveWindowRangeCallback_(-1, -1);
}

void WaveformOverview::mouseMove(const juce::MouseEvent& e)
{
  auto state = processor_.getPreparedState();
  if (!state || state->lengthInSamples == 0)
  {
    setMouseCursor(juce::MouseCursor::NormalCursor);
    return;
  }
  const float width = static_cast<float>(getWidth());
  const juce::int64 totalSamples = state->lengthInSamples;
  auto [startSample, endSample] = processor_.getWindowRangeSnappedToSlices();
  const float startX = sampleToX(startSample, totalSamples, width);
  const float endX = sampleToX(endSample, totalSamples, width);
  const float x = static_cast<float>(e.getPosition().getX());
  const int hit = hitTestWindow(x, startX, endX);
  if (hit == 0)
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
  else
    setMouseCursor(juce::MouseCursor::NormalCursor);
}
