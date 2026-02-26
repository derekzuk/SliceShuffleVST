#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../dsp/SlicerEngine.h"
#include <vector>
#include <cstddef>

/** Immutable state prepared on background thread; swapped into processor. */
struct PreparedState {
  juce::AudioBuffer<float> buffer;
  double sampleRate{0};
  juce::int64 lengthInSamples{0};
  std::vector<slicer::Slice> slices;
  /** Playback order: order[i] = source slice index for position i. */
  std::vector<size_t> playbackOrder;
};
