#include "BpmDetector.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <vector>

namespace {

constexpr double kAnalysisMaxSeconds = 10.0;
constexpr double kDownsampleRate = 11025.0;
constexpr double kEnvelopeRate = 500.0;  // downsample envelope further for cheaper autocorr
constexpr double kEnvelopeCutoffHz = 80.0;   // fast follower to capture hit shape (was 15)
constexpr double kEnvelopeSecondLowpassHz = 12.0;
constexpr double kOnsetSmoothHz = 8.0;
constexpr double kMinBpm = 40.0;
constexpr double kMaxBpm = 240.0;
constexpr int kCoarseLagStep = 10;
constexpr int kRefineRadius = 60;
constexpr double kMinConfidence = 0.15;
constexpr double kDominanceMargin = 0.05;  // best peak must exceed mean coarse score by this
constexpr double kHalfTimeDelta = 0.005;  // stricter: half-time (2*L) must be very close
constexpr double kDoubleTimeDelta = 0.02; // looser: double-time (L/2) or base L when close
constexpr double kEps = 1e-12;

/** Downmix to mono and downsample to targetRate. Returns empty if result would be too short. */
std::vector<float> downmixAndResample(const juce::AudioBuffer<float>& buffer, double sourceRate,
                                      double targetRate, int maxSamples)
{
  const int numCh = buffer.getNumChannels();
  const int numSamples = std::min(buffer.getNumSamples(), maxSamples);
  if (numCh == 0 || numSamples == 0)
    return {};

  const double ratio = sourceRate / targetRate;
  const int outLen = static_cast<int>(std::ceil(numSamples / ratio));
  if (outLen < 256)
    return {};

  std::vector<float> out(static_cast<size_t>(outLen), 0.f);
  const float gain = 1.f / static_cast<float>(numCh);

  std::vector<const float*> readPointers(static_cast<size_t>(numCh));
  for (int ch = 0; ch < numCh; ++ch)
    readPointers[static_cast<size_t>(ch)] = buffer.getReadPointer(ch);

  for (int i = 0; i < outLen; ++i)
  {
    const double srcIdx = i * ratio;
    const int i0 = static_cast<int>(srcIdx);
    const int i1 = std::min(i0 + 1, numSamples - 1);
    const float t = static_cast<float>(srcIdx - i0);
    float sum = 0.f;
    for (int ch = 0; ch < numCh; ++ch)
    {
      const float* p = readPointers[static_cast<size_t>(ch)];
      sum += (p[static_cast<size_t>(i0)] * (1.f - t) + p[static_cast<size_t>(i1)] * t) * gain;
    }
    out[static_cast<size_t>(i)] = sum;
  }
  return out;
}

/** Remove DC (subtract mean) in place. */
void removeDc(std::vector<float>& x)
{
  if (x.empty())
    return;
  const double mean = std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
  for (float& v : x)
    v -= static_cast<float>(mean);
}

/** One-pole lowpass: y[i] = alpha*y[i-1] + (1-alpha)*x[i]. Alpha = exp(-2*pi*fc/fs). */
void lowpassEnvelope(const float* x, size_t n, float alpha, std::vector<float>& out)
{
  if (n == 0)
    return;
  out.resize(n);
  out[0] = std::fabs(x[0]);
  for (size_t i = 1; i < n; ++i)
    out[i] = alpha * out[i - 1] + (1.f - alpha) * std::fabs(x[i]);
}

/** Downsample by block-mean (better anti-aliasing than stride sampling). */
std::vector<float> downsampleEnvelopeBlockMean(const std::vector<float>& env, int step)
{
  if (step <= 0 || env.size() < static_cast<size_t>(step))
    return {};

  const size_t nBlocks = env.size() / static_cast<size_t>(step);
  std::vector<float> out;
  out.reserve(nBlocks);

  size_t idx = 0;
  for (size_t b = 0; b < nBlocks; ++b)
  {
    double sum = 0.0;
    for (int k = 0; k < step; ++k)
      sum += env[idx++];
    out.push_back(static_cast<float>(sum / static_cast<double>(step)));
  }
  return out;
}

/** One-pole lowpass in place. fs = sample rate of x, cutoffHz = -3 dB point. */
void lowpassInPlace(std::vector<float>& x, double fs, double cutoffHz)
{
  if (x.empty())
    return;
  const float alpha = static_cast<float>(std::exp(-2.0 * 3.141592653589793 * cutoffHz / fs));
  for (size_t i = 1; i < x.size(); ++i)
    x[i] = alpha * x[i - 1] + (1.f - alpha) * x[i];
}

/** Normalized autocorrelation at lag: num/(den+eps), score in [-1,1]. */
double nacfAtLag(const float* x, size_t n, int lag)
{
  if (lag <= 0 || n <= static_cast<size_t>(lag))
    return 0.0;
  const size_t len = n - static_cast<size_t>(lag);
  double sumProd = 0.0, sumSq0 = 0.0, sumSqL = 0.0;
  for (size_t i = 0; i < len; ++i)
  {
    const double a = static_cast<double>(x[i]);
    const double b = static_cast<double>(x[i + static_cast<size_t>(lag)]);
    sumProd += a * b;
    sumSq0 += a * a;
    sumSqL += b * b;
  }
  const double den = std::sqrt(sumSq0 * sumSqL) + kEps;
  return sumProd / den;
}

/** Parabolic interpolation: given scores at lag-1, lag, lag+1, return sub-sample lag of peak. */
double parabolicPeak(int lag, double sPrev, double sCur, double sNext)
{
  const double denom = 2.0 * (sPrev - 2.0 * sCur + sNext);
  if (std::fabs(denom) < 1e-20)
    return static_cast<double>(lag);
  const double d = (sPrev - sNext) / denom;
  return static_cast<double>(lag) + std::clamp(d, -1.0, 1.0);
}

/** Compute BPM from lag at envelope sample rate. */
double lagToBpm(int lag, double envelopeRate)
{
  if (lag <= 0)
    return kMinBpm;
  return 60.0 * envelopeRate / static_cast<double>(lag);
}

struct Peak { int lag; double score; };

/** Find the best local maximum after the first local minimum (avoids tiny-lag subdivisions). */
std::optional<Peak> pickPeakAfterFirstDip(const float* x, size_t n,
                                          int minLag, int maxLag,
                                          int step)
{
  std::vector<double> s;
  std::vector<int> lags;
  for (int lag = minLag; lag <= maxLag; lag += step)
  {
    lags.push_back(lag);
    s.push_back(nacfAtLag(x, n, lag));
  }
  if (s.size() < 5)
    return std::nullopt;

  const size_t earlyN = std::min(s.size(), static_cast<size_t>(20));
  double maxEarly = s[0];
  for (size_t i = 1; i < earlyN; ++i)
    maxEarly = std::max(maxEarly, s[i]);
  maxEarly = std::max(maxEarly, 0.05);

  size_t dipIdx = 1;
  bool foundDip = false;
  for (size_t i = 1; i + 1 < s.size(); ++i)
  {
    const bool localMin = (s[i] < s[i - 1] && s[i] < s[i + 1]);
    const bool deepEnough = (s[i] < maxEarly - 0.05);
    if (localMin && deepEnough)
    {
      dipIdx = i;
      foundDip = true;
      break;
    }
  }
  if (!foundDip)
    dipIdx = 1;

  Peak best{lags[dipIdx], s[dipIdx]};
  bool found = false;
  for (size_t i = dipIdx + 1; i + 1 < s.size(); ++i)
  {
    if (s[i] > s[i - 1] && s[i] >= s[i + 1])
    {
      if (!found || s[i] > best.score)
      {
        best = {lags[i], s[i]};
        found = true;
      }
    }
  }
  if (!found)
    return std::nullopt;
  return best;
}

} // namespace

std::optional<double> detectBpm(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
  if (sampleRate <= 0.0 || buffer.getNumSamples() == 0)
    return std::nullopt;

  const int maxInputSamples = static_cast<int>(kAnalysisMaxSeconds * sampleRate);
  std::vector<float> mono = downmixAndResample(buffer, sampleRate, kDownsampleRate, maxInputSamples);
  if (mono.size() < 4096)
    return std::nullopt;

  removeDc(mono);

  const float alpha = static_cast<float>(std::exp(-2.0 * 3.141592653589793 * kEnvelopeCutoffHz / kDownsampleRate));
  std::vector<float> envelope;
  lowpassEnvelope(mono.data(), mono.size(), alpha, envelope);
  mono.clear();

  const int envStep = static_cast<int>(std::round(kDownsampleRate / kEnvelopeRate));
  if (envStep < 1)
    return std::nullopt;
  std::vector<float> envDs = downsampleEnvelopeBlockMean(envelope, envStep);
  envelope.clear();
  if (envDs.size() < 512)
    return std::nullopt;

  const double envRate = kDownsampleRate / static_cast<double>(envStep);
  lowpassInPlace(envDs, envRate, kEnvelopeSecondLowpassHz);

  std::vector<float> onset(envDs.size(), 0.f);
  for (size_t i = 1; i < envDs.size(); ++i)
  {
    const float d = envDs[i] - envDs[i - 1];
    onset[i] = (d > 0.f) ? d : 0.f;
  }
  for (auto& v : onset)
    v = std::sqrt(std::max(0.f, v));
  envDs.clear();
  lowpassInPlace(onset, envRate, kOnsetSmoothHz);
  removeDc(onset);

  const size_t n = onset.size();
  const float* x = onset.data();

  const int minLag = static_cast<int>(std::ceil(60.0 / kMaxBpm * envRate));
  const int maxLag = static_cast<int>(std::floor(60.0 / kMinBpm * envRate));
  if (minLag >= maxLag || maxLag >= static_cast<int>(n) - 1)
    return std::nullopt;

  auto peak = pickPeakAfterFirstDip(x, n, minLag, maxLag, kCoarseLagStep);
  if (!peak)
    return std::nullopt;

  int bestLag = peak->lag;
  double bestScore = peak->score;

  const int refineMin = std::max(minLag, bestLag - kRefineRadius);
  const int refineMax = std::min(maxLag, bestLag + kRefineRadius);
  for (int lag = refineMin; lag <= refineMax; ++lag)
  {
    const double s = nacfAtLag(x, n, lag);
    if (s > bestScore)
    {
      bestScore = s;
      bestLag = lag;
    }
  }

  double meanCoarseScore = 0.0;
  int numCoarse = 0;
  for (int lag = minLag; lag <= maxLag; lag += kCoarseLagStep)
  {
    meanCoarseScore += nacfAtLag(x, n, lag);
    ++numCoarse;
  }
  if (numCoarse > 0)
    meanCoarseScore /= static_cast<double>(numCoarse);
  if (bestScore < kMinConfidence || bestScore < meanCoarseScore + kDominanceMargin)
    return std::nullopt;

  double lagSub = static_cast<double>(bestLag);
  if (bestLag > refineMin && bestLag < refineMax)
  {
    const double sPrev = nacfAtLag(x, n, bestLag - 1);
    const double sNext = nacfAtLag(x, n, bestLag + 1);
    lagSub = parabolicPeak(bestLag, sPrev, bestScore, sNext);
  }

  const int L = bestLag;
  struct Candidate { int lag; double score; };
  std::vector<Candidate> candidates = {{L, bestScore}};
  auto tryLag = [&](int lag)
  {
    if (lag >= minLag && lag <= maxLag && lag < static_cast<int>(n))
      candidates.push_back({lag, nacfAtLag(x, n, lag)});
  };
  tryLag(2 * L);
  tryLag(L / 2);

  double bestCandScore = -1.0;
  for (const auto& c : candidates)
    bestCandScore = std::max(bestCandScore, c.score);

  auto bpmDist = [](double bpm) { return std::fabs(bpm - 120.0); };

  int chosenLag = L;
  for (const auto& c : candidates)
  {
    const double bpmC = lagToBpm(c.lag, envRate);
    if (bpmC < kMinBpm || bpmC > kMaxBpm)
      continue;
    const double delta = (c.lag == 2 * L) ? kHalfTimeDelta : kDoubleTimeDelta;
    if (c.score < bestCandScore - delta)
      continue;
    const double bpmChosen = lagToBpm(chosenLag, envRate);
    if (bpmDist(bpmC) < bpmDist(bpmChosen))
      chosenLag = c.lag;
  }

  double bpm;
  if (chosenLag == L && bestLag > refineMin && bestLag < refineMax)
    bpm = 60.0 * envRate / lagSub;
  else if (chosenLag != L)
  {
    const int rMin = std::max(minLag, chosenLag - kRefineRadius);
    const int rMax = std::min(maxLag, chosenLag + kRefineRadius);
    if (chosenLag > rMin && chosenLag < rMax)
    {
      const double sP = nacfAtLag(x, n, chosenLag - 1);
      const double sN = nacfAtLag(x, n, chosenLag + 1);
      const double sC = nacfAtLag(x, n, chosenLag);
      const double lagSubChosen = parabolicPeak(chosenLag, sP, sC, sN);
      bpm = 60.0 * envRate / lagSubChosen;
    }
    else
      bpm = lagToBpm(chosenLag, envRate);
  }
  else
    bpm = lagToBpm(chosenLag, envRate);

  bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
  return bpm;
}
