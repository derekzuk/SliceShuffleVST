#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PreparedState.h"

/** Strip showing the full-length waveform with a draggable window that defines the visible range. */
class WaveformOverview : public juce::Component
{
public:
  explicit WaveformOverview(SlicerPluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent&) override;

private:
  /** Return which part of the window rect was hit: -1 left edge, 0 body, 1 right edge, -2 none. */
  int hitTestWindow(float x, float windowStartX, float windowEndX) const;
  /** Map sample position to x in [0, width]. */
  float sampleToX(juce::int64 sample, juce::int64 totalSamples, float width) const;
  /** Map x in [0, width] to sample position. */
  juce::int64 xToSample(float x, juce::int64 totalSamples, float width) const;
  /** Set processor window from slice range (updates APVTS). */
  void setWindowFromSliceRange(size_t startSliceIdx, size_t endSliceIdxExclusive);

  static constexpr float kEdgeHitWidthPx = 6.0f;
  static constexpr int kMinWindowSlices = 4;
  static constexpr int kMaxWindowSlices = 64;

  SlicerPluginProcessor& processor_;
  int dragHit_{-2};       // -2 none, -1 left edge, 0 body, 1 right edge
  float dragStartX_{0};   // for move: offset from window left
  juce::int64 dragStartSampleStart_{0};
  juce::int64 dragStartSampleEnd_{0};
};
