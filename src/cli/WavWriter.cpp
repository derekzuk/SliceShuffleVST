#include "WavWriter.h"

bool writeWav(const juce::File& file,
              const juce::AudioBuffer<float>& buffer,
              double sampleRate,
              int numBitsPerSample) {
  file.deleteFile();
  juce::WavAudioFormat format;
  std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
  if (!static_cast<juce::FileOutputStream*>(stream.get())->openedOk())
    return false;

  using Opts = juce::AudioFormatWriterOptions;
  auto opts = Opts{}
    .withSampleRate(sampleRate)
    .withNumChannels(buffer.getNumChannels())
    .withBitsPerSample(numBitsPerSample);
  if (numBitsPerSample == 32)
    opts = opts.withSampleFormat(Opts::SampleFormat::floatingPoint);

  std::unique_ptr<juce::AudioFormatWriter> writer = format.createWriterFor(stream, opts);

  if (writer == nullptr)
    return false;

  const bool ok = writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
  writer->flush();
  return ok;
}
