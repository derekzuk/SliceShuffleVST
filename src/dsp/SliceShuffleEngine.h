#pragma once

#include <vector>
#include <cstddef>

namespace sliceshuffle {

// Sample buffer: interleaved or per-channel; here we use a flat buffer for simplicity.
// You can switch to std::vector<std::vector<float>> for per-channel later.
using SampleBuffer = std::vector<float>;

struct Slice {
  size_t startSample{0};
  size_t lengthSamples{0};
};

/**
 * BPM-based slicer: given a buffer and sample rate, slice on a BPM grid
 * and return slice descriptors. Shuffle is applied to the order of slices;
 * the actual rearrange will be done when writing output.
 *
 * This layer is JUCE-free so it can be unit-tested without a DAW.
 */
class SliceShuffleEngine {
public:
  SliceShuffleEngine() = default;

  /** Set BPM (used to compute slice length from sample rate). */
  void setBpm(double bpm) { bpm_ = bpm; }
  double getBpm() const { return bpm_; }

  /**
   * Compute slice boundaries for the given buffer length and sample rate.
   * Slice length = (60.0 / bpm) * sampleRate * beatsPerSlice (default 1 beat).
   */
  std::vector<Slice> computeSlices(size_t totalSamples,
                                   double sampleRate,
                                   double beatsPerSlice = 1.0) const;

  /**
   * Return a random permutation of indices [0, numSlices).
   * \param seed 0 = use random_device (non-reproducible).
   * \param noImmediateRepeats if true, ensure no two adjacent output slices are the same (avoids obvious repeats).
   */
  static std::vector<size_t> shuffledSliceOrder(size_t numSlices,
                                                 uint32_t seed = 0,
                                                 bool noImmediateRepeats = true);

private:
  double bpm_{120.0};
};

} // namespace sliceshuffle
