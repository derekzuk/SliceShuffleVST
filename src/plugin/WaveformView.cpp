#include "WaveformView.h"
#include <vector>

namespace {
constexpr const char* kBpmId = "bpm";
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
} // namespace

WaveformView::WaveformView(SlicerPluginProcessor& proc) : processor_(proc)
{
  formatManager_.registerBasicFormats();
  thumbnail_.addChangeListener(this);
  setOpaque(true);
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

  // Always draw from buffer so the view updates after Rearrange; use min/max envelope for a normal waveform look
  const int numChannels = state->buffer.getNumChannels();
  const int numSamples = state->buffer.getNumSamples();
  if (numSamples > 0)
  {
    g.setColour(juce::Colour(0xff404060));
    const float scale = height * 0.45f;
    const float midY = height * 0.5f;
    const int w = static_cast<int>(width);
    std::vector<float> maxPerCol(static_cast<size_t>(w), -1.0f);
    std::vector<float> minPerCol(static_cast<size_t>(w), 1.0f);
    for (int x = 0; x < w; ++x)
    {
      const int lo = static_cast<int>((static_cast<float>(x) / width) * numSamples);
      const int hi = juce::jmin(numSamples, static_cast<int>((static_cast<float>(x + 1) / width) * numSamples) + 1);
      if (hi <= lo) continue;
      float mx = -1.0f;
      float mn = 1.0f;
      for (int ch = 0; ch < numChannels; ++ch)
      {
        const float* data = state->buffer.getReadPointer(ch);
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

  // Slice grid overlay
  g.setColour(juce::Colour(0x88ffffff));
  for (const auto& slice : state->slices)
  {
    const float x = (static_cast<float>(slice.startSample) / static_cast<float>(totalSamples)) * width;
    g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
  }
  // Right edge of last slice
  if (!state->slices.empty())
  {
    const auto& last = state->slices.back();
    const float x = (static_cast<float>(last.startSample + last.lengthSamples) /
                    static_cast<float>(totalSamples)) *
                   width;
    g.drawVerticalLine(static_cast<int>(x), 0.0f, height);
  }

  // Window overlay: show which region will be rearranged
  const double bpm = static_cast<double>(processor_.getValueTreeState().getRawParameterValue(kBpmId)->load());
  if (bpm > 0.0 && state->sampleRate > 0.0)
  {
    const double secondsPerBeat = 60.0 / bpm;
    const double totalBeats = static_cast<double>(totalSamples) / (state->sampleRate * secondsPerBeat);
    const int windowBeatsParam = juce::jlimit(
        2, 64,
        static_cast<int>(std::round(processor_.getValueTreeState().getRawParameterValue(kWindowBeatsId)->load())));
    const double windowBeats = static_cast<double>(windowBeatsParam);
    const float posNorm = processor_.getValueTreeState().getRawParameterValue(kWindowPositionId)->load();
    const double startBeat = totalBeats <= windowBeats ? 0.0 : static_cast<double>(posNorm) * std::max(0.0, totalBeats - windowBeats);
    const size_t startSample = static_cast<size_t>(std::round(startBeat * secondsPerBeat * state->sampleRate));
    const size_t endSample = std::min(
        static_cast<size_t>(totalSamples),
        static_cast<size_t>(std::round((startBeat + windowBeats) * secondsPerBeat * state->sampleRate)));
    if (endSample > startSample)
    {
      const float startX = (static_cast<float>(startSample) / static_cast<float>(totalSamples)) * width;
      const float endX = (static_cast<float>(endSample) / static_cast<float>(totalSamples)) * width;
      const float w = std::max(2.0f, endX - startX);
      g.setColour(juce::Colour(0x2800aaff));
      g.fillRect(startX, 0.0f, w, height);
      g.setColour(juce::Colour(0xc00080ff));
      g.drawVerticalLine(static_cast<int>(startX), 0.0f, height);
      g.drawVerticalLine(static_cast<int>(endX), 0.0f, height);
    }
  }
}

void WaveformView::resized() {}

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
    if (currentFile_.existsAsFile())
      thumbnail_.setSource(new juce::FileInputSource(currentFile_));
    else
      thumbnail_.clear();
  }
  repaint();
}
