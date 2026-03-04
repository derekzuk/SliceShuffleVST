#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <optional>

/** Estimate BPM from an audio buffer using autocorrelation (best for beat-heavy material).
 *  Returns nullopt if detection fails or buffer is too short.
 *  Result is clamped to the range [40, 240] BPM. */
std::optional<double> detectBpm(const juce::AudioBuffer<float>& buffer, double sampleRate);
