#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <optional>

struct LoadedWav {
  juce::AudioBuffer<float> buffer;
  double sampleRate{0};
  int numChannels{0};
  juce::int64 lengthInSamples{0};
};

/** Load a WAV (or other supported format) into memory. Returns nullopt on failure. */
std::optional<LoadedWav> loadWav(const juce::File& file);
