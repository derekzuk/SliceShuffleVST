#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PreparedState.h"
#include <atomic>
#include <functional>
#include <vector>

/** Strip showing the full-length waveform with a draggable window that defines the visible range. */
class WaveformOverview : public juce::Component
{
public:
  /** Called with (startSample, endSample) during drag; (-1, -1) when drag ends. */
  using LiveWindowRangeCallback = std::function<void(juce::int64, juce::int64)>;

  explicit WaveformOverview(CutShufflePluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent&) override;
  void mouseMove(const juce::MouseEvent& e) override;

  /** Set callback to notify current window range during drag (for live bottom waveform). */
  void setLiveWindowRangeCallback(LiveWindowRangeCallback cb) { liveWindowRangeCallback_ = std::move(cb); }

  /** Call when sample may have loaded or state changed; triggers envelope rebuild if needed. */
  void ensureEnvelopeBuilt();

private:
  /** Return which part of the window rect was hit: 0 = window (drag to move), -2 = outside. */
  int hitTestWindow(float x, float windowStartX, float windowEndX) const;
  /** Map sample position to x in [0, width]. */
  float sampleToX(juce::int64 sample, juce::int64 totalSamples, float width) const;
  /** Map x in [0, width] to sample position. */
  juce::int64 xToSample(float x, juce::int64 totalSamples, float width) const;
  /** Set processor window from slice range (updates APVTS). Only use on mouseUp. */
  void setWindowFromSliceRange(size_t startSliceIdx, size_t endSliceIdxExclusive);
  /** Return the slice boundary (sample position) closest to the given sample. */
  juce::int64 nearestSliceBoundarySample(juce::int64 sample) const;
  /** Given a boundary sample position, return start slice index (0 or index+1 where sliceEnds_[index]==boundary). */
  size_t boundaryToStartSliceIndex(juce::int64 boundarySample) const;
  /** Given a boundary sample position (end of a slice), return exclusive end slice index. */
  size_t boundaryToEndSliceIndexExclusive(juce::int64 boundarySample) const;
  /** Rebuild min/max envelope on background thread; callAsync to swap and repaint. */
  void rebuildEnvelopeAsync();
  /** Binary search: slice index containing sample (using cached sliceEnds_). */
  size_t sampleToSliceIndexBinary(juce::int64 sample) const;

  static constexpr float kEdgeHitWidthPx = 12.0f;
  static constexpr int kMinWindowSlices = 4;
  static constexpr int kMaxWindowSlices = 64;

  CutShufflePluginProcessor& processor_;
  juce::ThreadPool envelopePool_{1};

  // Cached envelope (built async)
  std::vector<float> maxPerCol_;
  std::vector<float> minPerCol_;
  int cachedWidth_{0};
  juce::int64 cachedLengthInSamples_{0};
  std::atomic<bool> envelopeReady_{false};
  std::atomic<uint64_t> envelopeJobId_{0};

  // Drag state: -2 none, 0 window (move only; no edge resize)
  int dragHit_{-2};
  float dragStartX_{0};
  juce::int64 dragStartSampleStart_{0};
  juce::int64 dragStartSampleEnd_{0};
  /** During drag: window in sample space (continuous, no snap). -1 means use APVTS. */
  juce::int64 dragWindowStartSample_{-1};
  juce::int64 dragWindowEndSample_{-1};
  /** Precomputed slice end samples for binary search (set in mouseDown). */
  std::vector<juce::int64> sliceEnds_;

  LiveWindowRangeCallback liveWindowRangeCallback_;
};
