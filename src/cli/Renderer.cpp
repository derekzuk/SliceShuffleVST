#include "Renderer.h"
#include <algorithm>
#include <cmath>

namespace {

// Cosine curve 0..1 over [0,1] for smooth fades
float fadeGain(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return 0.5f * (1.f - std::cos(t * static_cast<float>(juce::MathConstants<float>::pi)));
}

} // namespace

void renderSliced(juce::AudioBuffer<float>& out,
                 const juce::AudioBuffer<float>& source,
                 const std::vector<sliceshuffle::Slice>& slices,
                 const std::vector<size_t>& order,
                 double sampleRate,
                 double crossfadeMs) {
  const int numChannels = source.getNumChannels();
  const size_t numSlices = order.size();
  if (numSlices == 0 || numChannels != out.getNumChannels()) return;

  const auto crossfadeSamples = static_cast<size_t>(std::round((crossfadeMs / 1000.0) * sampleRate));
  size_t outPos = 0;

  for (size_t i = 0; i < numSlices; ++i) {
    const size_t srcSliceIdx = order[i];
    if (srcSliceIdx >= slices.size()) continue;
    const sliceshuffle::Slice& sl = slices[srcSliceIdx];
    const size_t len = sl.lengthSamples;
    const size_t fadeLen = std::min(crossfadeSamples, len / 2);

    for (int ch = 0; ch < numChannels; ++ch) {
      const float* src = source.getReadPointer(ch, static_cast<int>(sl.startSample));
      float* dst = out.getWritePointer(ch, static_cast<int>(outPos));

      for (size_t k = 0; k < len; ++k) {
        float g = 1.f;
        if (fadeLen > 0) {
          if (k < fadeLen)
            g = fadeGain(static_cast<float>(k) / static_cast<float>(fadeLen));
          else if (k >= len - fadeLen)
            g = fadeGain(static_cast<float>(len - 1 - k) / static_cast<float>(fadeLen));
        }
        dst[k] = src[k] * g;
      }
    }
    outPos += len;
  }
}
