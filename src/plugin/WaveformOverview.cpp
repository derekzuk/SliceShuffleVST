#include "WaveformOverview.h"
#include <cmath>
#include <vector>

namespace {
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
} // namespace

WaveformOverview::WaveformOverview(SlicerPluginProcessor& proc) : processor_(proc)
{
  setOpaque(true);
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
  if (x < windowStartX - kEdgeHitWidthPx)
    return -2;
  if (x < windowStartX + kEdgeHitWidthPx)
    return -1;
  if (x <= windowEndX - kEdgeHitWidthPx)
    return 0;
  if (x <= windowEndX + kEdgeHitWidthPx)
    return 1;
  return -2;
}

void WaveformOverview::setWindowFromSliceRange(size_t startSliceIdx, size_t endSliceIdxExclusive)
{
  (void)endSliceIdxExclusive; // we only set position; window size comes from the slider
  auto state = processor_.getPreparedState();
  if (!state || state->slices.empty())
    return;
  const size_t numSlices = state->slices.size();
  juce::AudioProcessorValueTreeState& apvts = processor_.getValueTreeState();

  // Keep the current Window # (slices) unchanged; only update position.
  // AudioParameterInt stores the actual value 4..64, not normalized.
  const float rawWindow = static_cast<float>(apvts.getRawParameterValue(kWindowBeatsId)->load());
  int windowSlices = juce::jlimit(kMinWindowSlices, kMaxWindowSlices, static_cast<int>(std::round(rawWindow)));
  windowSlices = (windowSlices + 1) & ~1; // even: 4, 6, 8, ...

  const size_t maxStart = numSlices >= static_cast<size_t>(windowSlices)
                              ? numSlices - static_cast<size_t>(windowSlices)
                              : 0;
  startSliceIdx = juce::jmin(startSliceIdx, maxStart);
  const float posNorm = (maxStart > 0) ? (static_cast<float>(startSliceIdx) / static_cast<float>(maxStart)) : 0.0f;

  apvts.getParameterAsValue(kWindowPositionId).setValue(static_cast<double>(posNorm));
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
  const int numChannels = state->buffer.getNumChannels();
  const int numSamples = state->buffer.getNumSamples();

  if (numSamples <= 0)
    return;

  // Full-length waveform (min/max envelope)
  g.setColour(juce::Colour(0xff404060));
  const float scale = height * 0.45f;
  const float midY = height * 0.5f;
  const int w = static_cast<int>(width);
  std::vector<float> maxPerCol(static_cast<size_t>(w), -1.0f);
  std::vector<float> minPerCol(static_cast<size_t>(w), 1.0f);
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
      const float* data = state->buffer.getReadPointer(ch);
      for (int i = lo; i < hi; ++i)
      {
        const float s = data[i];
        if (s > mx)
          mx = s;
        if (s < mn)
          mn = s;
      }
    }
    maxPerCol[static_cast<size_t>(x)] = mx;
    minPerCol[static_cast<size_t>(x)] = mn;
  }
  juce::Path p;
  p.startNewSubPath(0.0f, midY);
  for (int x = 0; x < w; ++x)
    p.lineTo(static_cast<float>(x),
             juce::jlimit(0.0f, height, midY - maxPerCol[static_cast<size_t>(x)] * scale));
  for (int x = w - 1; x >= 0; --x)
    p.lineTo(static_cast<float>(x),
             juce::jlimit(0.0f, height, midY - minPerCol[static_cast<size_t>(x)] * scale));
  p.closeSubPath();
  g.fillPath(p);

  // Current window overlay
  auto [startSample, endSample] = processor_.getWindowRangeSnappedToSlices();
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
}

void WaveformOverview::mouseDrag(const juce::MouseEvent& e)
{
  auto state = processor_.getPreparedState();
  if (!state || state->slices.empty() || state->lengthInSamples <= 0)
    return;

  const float width = static_cast<float>(getWidth());
  const juce::int64 totalSamples = state->lengthInSamples;
  const float x = static_cast<float>(e.getPosition().getX());
  const std::vector<slicer::Slice>& slices = state->slices;
  const size_t numSlices = slices.size();

  auto sampleToSliceIndex = [&](juce::int64 sample) -> size_t
  {
    if (sample <= 0)
      return 0;
    for (size_t i = 0; i < numSlices; ++i)
    {
      const juce::int64 end = static_cast<juce::int64>(slices[i].startSample + slices[i].lengthSamples);
      if (sample < end)
        return i;
    }
    return numSlices > 0 ? numSlices - 1 : 0;
  };

  if (dragHit_ == 0)
  {
    // Move window: keep same length, change position
    const float dx = x - dragStartX_;
    const float samplePerPx = totalSamples / juce::jmax(1.0f, width);
    const juce::int64 deltaSample = static_cast<juce::int64>(dx * samplePerPx);
    juce::int64 newStart = dragStartSampleStart_ + deltaSample;
    juce::int64 newEnd = dragStartSampleEnd_ + deltaSample;
    newStart = juce::jlimit(juce::int64(0), totalSamples, newStart);
    newEnd = juce::jlimit(juce::int64(0), totalSamples, newEnd);
    if (newEnd <= newStart)
      newEnd = juce::jmin(totalSamples, newStart + (dragStartSampleEnd_ - dragStartSampleStart_));
    size_t si = sampleToSliceIndex(newStart);
    size_t ei = sampleToSliceIndex(newEnd - 1);
    if (ei < si)
      ei = si;
    ei += 1; // exclusive end
    setWindowFromSliceRange(si, ei);
  }
  else if (dragHit_ == -1)
  {
    // Resize left edge
    juce::int64 newStart = xToSample(x, totalSamples, width);
    newStart = juce::jlimit(juce::int64(0), dragStartSampleEnd_ - 1, newStart);
    size_t si = sampleToSliceIndex(newStart);
    size_t ei = sampleToSliceIndex(dragStartSampleEnd_ - 1);
    if (ei < si)
      ei = si;
    ei += 1;
    setWindowFromSliceRange(si, ei);
  }
  else if (dragHit_ == 1)
  {
    // Resize right edge
    juce::int64 newEnd = xToSample(x, totalSamples, width);
    newEnd = juce::jlimit(dragStartSampleStart_ + 1, totalSamples, newEnd);
    size_t si = sampleToSliceIndex(dragStartSampleStart_);
    size_t ei = sampleToSliceIndex(newEnd - 1);
    if (ei < si)
      ei = si;
    ei += 1;
    setWindowFromSliceRange(si, ei);
  }
  else
  {
    // Drag from empty area: set window to dragged range
    juce::int64 a = xToSample(juce::jmin(x, dragStartX_), totalSamples, width);
    juce::int64 b = xToSample(juce::jmax(x, dragStartX_), totalSamples, width);
    if (b <= a)
      b = juce::jmin(totalSamples, a + 1);
    size_t si = sampleToSliceIndex(a);
    size_t ei = sampleToSliceIndex(b - 1);
    if (ei < si)
      ei = si;
    ei += 1;
    setWindowFromSliceRange(si, ei);
  }

  repaint();
}

void WaveformOverview::mouseUp(const juce::MouseEvent&)
{
  dragHit_ = -2;
}
