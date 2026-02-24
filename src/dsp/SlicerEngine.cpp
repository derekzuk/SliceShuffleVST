#include "SlicerEngine.h"
#include <random>
#include <algorithm>
#include <numeric>

namespace slicer {

std::vector<Slice> SlicerEngine::computeSlices(size_t totalSamples,
                                               double sampleRate,
                                               double beatsPerSlice) const {
  std::vector<Slice> slices;
  if (bpm_ <= 0.0 || sampleRate <= 0.0) return slices;

  const double secondsPerBeat = 60.0 / bpm_;
  const double secondsPerSlice = secondsPerBeat * beatsPerSlice;
  const size_t samplesPerSlice = static_cast<size_t>(secondsPerSlice * sampleRate);
  if (samplesPerSlice == 0) return slices;

  size_t pos = 0;
  while (pos < totalSamples) {
    size_t len = std::min(samplesPerSlice, totalSamples - pos);
    slices.push_back({pos, len});
    pos += len;
  }
  return slices;
}

std::vector<size_t> SlicerEngine::shuffledSliceOrder(size_t numSlices, uint32_t seed) {
  std::vector<size_t> indices(numSlices);
  std::iota(indices.begin(), indices.end(), size_t(0));
  std::mt19937 rng(seed != 0 ? seed : std::random_device{}());
  std::shuffle(indices.begin(), indices.end(), rng);
  return indices;
}

} // namespace slicer
