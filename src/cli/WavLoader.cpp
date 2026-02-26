#include "WavLoader.h"

std::optional<LoadedWav> loadWav(const juce::File& file) {
  if (!file.existsAsFile())
    return std::nullopt;

  juce::AudioFormatManager formatManager;
  formatManager.registerBasicFormats();

  std::unique_ptr<juce::AudioFormatReader> reader(
    formatManager.createReaderFor(file));

  if (reader == nullptr)
    return std::nullopt;

  const auto numChannels = static_cast<int>(reader->numChannels);
  const auto lengthInSamples = reader->lengthInSamples;

  if (numChannels <= 0 || lengthInSamples <= 0)
    return std::nullopt;

  LoadedWav result;
  result.sampleRate = reader->sampleRate;
  result.numChannels = numChannels;
  result.lengthInSamples = lengthInSamples;
  result.buffer.setSize(numChannels, static_cast<int>(lengthInSamples), false, false, true);

  if (!reader->read(&result.buffer, 0, static_cast<int>(lengthInSamples), 0, true, true))
    return std::nullopt;

  return result;
}
