// Slicer CLI: load WAV → slice by BPM → shuffle slices → write WAV.
// Usage (to be implemented): SlicerCli input.wav output.wav --bpm 120

#include <juce_core/juce_core.h>
#include "SlicerEngine.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
  juce::ignoreUnused(argc, argv);

  std::cout << "Slicer CLI — BPM slice & shuffle (stub).\n";
  std::cout << "Planned usage: SlicerCli <input.wav> <output.wav> --bpm <bpm>\n";

  // Minimal sanity check: DSP layer works without JUCE
  slicer::SlicerEngine engine;
  engine.setBpm(120.0);
  double sampleRate = 44100.0;
  size_t totalSamples = 44100 * 4; // 4 seconds
  auto slices = engine.computeSlices(totalSamples, sampleRate, 1.0);
  auto order = slicer::SlicerEngine::shuffledSliceOrder(slices.size(), 42u);
  std::cout << "Slices: " << slices.size() << ", shuffled order size: " << order.size() << "\n";

  return 0;
}
