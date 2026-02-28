#include "CutShuffleEngine.h"
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace cutshuffle {

std::vector<Slice> CutShuffleEngine::computeSlices(size_t totalSamples,
                                               double sampleRate,
                                               double beatsPerSlice) const {
  std::vector<Slice> slices;
  if (bpm_ <= 0.0 || sampleRate <= 0.0) return slices;

  // Sample-accurate: compute in double, round once to avoid drift
  const double secondsPerBeat = 60.0 / bpm_;
  const double secondsPerSlice = secondsPerBeat * beatsPerSlice;
  const size_t samplesPerSlice = static_cast<size_t>(std::round(secondsPerSlice * sampleRate));
  if (samplesPerSlice == 0) return slices;

  size_t pos = 0;
  while (pos < totalSamples) {
    size_t len = std::min(samplesPerSlice, totalSamples - pos);
    slices.push_back({pos, len});
    pos += len;
  }
  return slices;
}

std::vector<size_t> CutShuffleEngine::shuffledSliceOrder(size_t numSlices,
                                                      uint32_t seed,
                                                      bool noImmediateRepeats) {
  if (numSlices == 0) return {};
  std::vector<size_t> indices(numSlices);
  std::iota(indices.begin(), indices.end(), size_t(0));
  std::mt19937 rng(seed != 0 ? seed : std::random_device{}());
  std::shuffle(indices.begin(), indices.end(), rng);

  if (!noImmediateRepeats || numSlices <= 1) return indices;

  // Remove adjacent repeats by swapping (so new value at i differs from i-1, and we don't create repeats at j)
  const size_t maxPasses = numSlices * 2;
  std::uniform_int_distribution<size_t> dist(0, numSlices - 1);
  for (size_t pass = 0; pass < maxPasses; ++pass) {
    bool anyRepeat = false;
    for (size_t i = 1; i < numSlices; ++i) {
      if (indices[i] == indices[i - 1]) {
        anyRepeat = true;
        const size_t needDifferentFrom = indices[i - 1];
        size_t j = dist(rng);
        int attempts = 0;
        while (attempts < 1000 &&
               (j == i || indices[j] == needDifferentFrom ||
                (j > 0 && indices[j - 1] == indices[i]) ||
                (j + 1 < numSlices && indices[j + 1] == indices[i]))) {
          j = dist(rng);
          ++attempts;
        }
        if (attempts < 1000)
          std::swap(indices[i], indices[j]);
      }
    }
    if (!anyRepeat) break;
  }
  return indices;
}

} // namespace cutshuffle
