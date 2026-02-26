#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

/** Write buffer to WAV (32-bit float). Returns false on failure. */
bool writeWav(const juce::File& file,
              const juce::AudioBuffer<float>& buffer,
              double sampleRate,
              int numBitsPerSample = 32);
