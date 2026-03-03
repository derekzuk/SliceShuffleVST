#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../dsp/SliceShuffleEngine.h"
#include <vector>
#include <cstddef>
#include <unordered_set>

/** Immutable state prepared on background thread; swapped into processor. */
struct PreparedState {
  juce::AudioBuffer<float> buffer;
  double sampleRate{0};
  juce::int64 lengthInSamples{0};
  std::vector<sliceshuffle::Slice> slices;
  /** Playback order: order[i] = source (physical) slice index for logical position i. */
  std::vector<size_t> playbackOrder;
  /** Logical positions (indices into playbackOrder) that should be treated as silent (non-destructive mute). */
  std::unordered_set<size_t> mutedLogicalPositions;
};
