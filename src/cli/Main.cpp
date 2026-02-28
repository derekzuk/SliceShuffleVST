// CutShuffle CLI: load WAV → slice by BPM → shuffle slices → render with crossfades → write WAV.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "CutShuffleEngine.h"
#include "WavLoader.h"
#include "WavWriter.h"
#include "Renderer.h"
#include <iostream>
#include <string>
#include <cstdlib>

static void printUsage(const char* exe) {
  std::cerr << "Usage: " << exe << " <input.wav> <output.wav> --bpm <bpm> [options]\n"
            << "  --bpm <bpm>              BPM for grid (required)\n"
            << "  --granularity <n>         Slices per beat (default: 1). Higher = finer chops (2, 4, 8...)\n"
            << "  --beats-per-slice <n>     Alternative: beats per slice (default: 1). Overridden by --granularity\n"
            << "  --seed <n>               Shuffle seed, 0 = random (default: 0)\n"
            << "  --crossfade-ms <ms>       Crossfade at slice edges in ms (default: 5)\n";
}

int main(int argc, char* argv[]) {
  juce::ScopedJuceInitialiser_GUI init;

  if (argc < 5) {
    printUsage(argv[0]);
    return 1;
  }

  const std::string inputPath(argv[1]);
  const std::string outputPath(argv[2]);

  double bpm = 0;
  double beatsPerSlice = 1.0;
  double granularity = 0;   // 0 = not set; if set, beatsPerSlice = 1 / granularity
  uint32_t seed = 0;
  double crossfadeMs = 5.0;

  for (int i = 3; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--bpm" && i + 1 < argc) {
      bpm = std::atof(argv[++i]);
    } else if (arg == "--granularity" && i + 1 < argc) {
      granularity = std::atof(argv[++i]);
    } else if (arg == "--beats-per-slice" && i + 1 < argc) {
      beatsPerSlice = std::atof(argv[++i]);
    } else if (arg == "--seed" && i + 1 < argc) {
      seed = static_cast<uint32_t>(std::atoi(argv[++i]));
    } else if (arg == "--crossfade-ms" && i + 1 < argc) {
      crossfadeMs = std::atof(argv[++i]);
    }
  }

  if (granularity > 0)
    beatsPerSlice = 1.0 / granularity;

  if (bpm <= 0) {
    std::cerr << "Error: --bpm must be positive.\n";
    printUsage(argv[0]);
    return 1;
  }
  if (beatsPerSlice <= 0) {
    std::cerr << "Error: --granularity and --beats-per-slice must be positive.\n";
    printUsage(argv[0]);
    return 1;
  }

  juce::File inputFile(inputPath);
  auto loaded = loadWav(inputFile);
  if (!loaded) {
    std::cerr << "Error: could not load " << inputPath << "\n";
    return 1;
  }

  const size_t totalSamples = static_cast<size_t>(loaded->lengthInSamples);
  cutshuffle::CutShuffleEngine engine;
  engine.setBpm(bpm);
  auto slices = engine.computeSlices(totalSamples, loaded->sampleRate, beatsPerSlice);
  if (slices.empty()) {
    std::cerr << "Error: no slices (check BPM and file length).\n";
    return 1;
  }

  auto order = cutshuffle::CutShuffleEngine::shuffledSliceOrder(slices.size(), seed, true);
  std::cout << "Slices: " << slices.size() << ", BPM: " << bpm
            << ", beats/slice: " << beatsPerSlice << ", seed: " << seed << "\n";

  // Output length = sum of slice lengths (same as input when just reordering)
  size_t outLength = 0;
  for (const auto& sl : slices)
    outLength += sl.lengthSamples;

  juce::AudioBuffer<float> out(loaded->numChannels, static_cast<int>(outLength));
  out.clear();
  renderSliced(out, loaded->buffer, slices, order, loaded->sampleRate, crossfadeMs);

  juce::File outputFile(outputPath);
  if (!writeWav(outputFile, out, loaded->sampleRate, 32)) {
    std::cerr << "Error: could not write " << outputPath << "\n";
    return 1;
  }

  std::cout << "Wrote " << outputPath << " (" << outLength << " samples, " << loaded->sampleRate << " Hz)\n";
  return 0;
}
