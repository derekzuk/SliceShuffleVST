#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../cli/WavLoader.h"
#include "../dsp/CutShuffleEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr const char* kBpmId = "bpm";
constexpr const char* kGranularityId = "granularity";
constexpr const char* kSeedId = "seed";
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
constexpr const char* kStateVersionAttr = "stateVersion";
constexpr const char* kSamplePathAttr = "samplePath";
constexpr const char* kPlaybackOrderAttr = "playbackOrder";

constexpr int kCurrentStateVersion = 1;

// Beats per slice for each granularity index 0..4
// Order: 1/4, 1/2, 1, 2, 4 beats
constexpr double kBeatsPerSlice[] = {0.25, 0.5, 1.0, 2.0, 4.0};
constexpr int kNumGranularityChoices = 5;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kBpmId, 1}, "BPM",
      juce::NormalisableRange<float>(40.f, 240.f, 0.1f, 1.f), 120.f));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{kGranularityId, 1}, "Granularity",
      juce::StringArray{"1/4", "1/2", "1", "2", "4"}, 1)); // index 1 = "1/2" beat default
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kSeedId, 1}, "Seed", 0, 999999, 0));
  // Window = number of slices (min 4); we use even count only (4, 6, 8, …)
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kWindowBeatsId, 1}, "Window", 4, 64, 16));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kWindowPositionId, 1}, "Window position",
      juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f), 0.f));
  return layout;
}
} // namespace

CutShufflePluginProcessor::CutShufflePluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(
        BusesProperties()
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "CutShuffleParams", createParameterLayout())
#endif
{
#ifdef JucePlugin_PreferredChannelConfigurations
  apvts = juce::AudioProcessorValueTreeState(*this, nullptr, "CutShuffleParams", createParameterLayout());
#endif
  voices_.resize(kMaxVoices);
}

CutShufflePluginProcessor::~CutShufflePluginProcessor() = default;

const juce::String CutShufflePluginProcessor::getName() const { return JucePlugin_Name; }

bool CutShufflePluginProcessor::acceptsMidi() const { return true; }
bool CutShufflePluginProcessor::producesMidi() const { return false; }
bool CutShufflePluginProcessor::isMidiEffect() const { return false; }
double CutShufflePluginProcessor::getTailLengthSeconds() const { return 0.0; }

int CutShufflePluginProcessor::getNumPrograms() { return 1; }
int CutShufflePluginProcessor::getCurrentProgram() { return 0; }
void CutShufflePluginProcessor::setCurrentProgram(int) {}
const juce::String CutShufflePluginProcessor::getProgramName(int) { return {}; }
void CutShufflePluginProcessor::changeProgramName(int, const juce::String&) {}

void CutShufflePluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  hostSampleRate_ = sampleRate;
  hostBlockSize_ = samplesPerBlock;
}

void CutShufflePluginProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool CutShufflePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
      || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

std::shared_ptr<const PreparedState> CutShufflePluginProcessor::getPreparedState() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_;
}

CutShufflePluginProcessor::LoadStatus CutShufflePluginProcessor::getLoadStatus() const
{
  return loadStatus_.load();
}

juce::String CutShufflePluginProcessor::getLoadedSampleDisplayName() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSampleDisplayName_;
}

juce::String CutShufflePluginProcessor::getLoadedSamplePath() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSamplePath_;
}

int CutShufflePluginProcessor::getNumSlices() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_ ? static_cast<int>(preparedState_->slices.size()) : 0;
}

juce::String CutShufflePluginProcessor::getLoadErrorText() const
{
  std::shared_lock lock(stateMutex_);
  return loadErrorText_;
}

void CutShufflePluginProcessor::setLoadStatus(LoadStatus s)
{
  loadStatus_.store(s);
}

void CutShufflePluginProcessor::setLoadError(const juce::String& text)
{
  std::unique_lock lock(stateMutex_);
  loadErrorText_ = text;
}

void CutShufflePluginProcessor::applyNewPreparedState(std::shared_ptr<const PreparedState> state)
{
  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_ = std::move(state);
  }
  setLoadStatus(state ? LoadStatus::Ready : LoadStatus::Idle);
  if (!state)
    setLoadError({});
}

void CutShufflePluginProcessor::buildPreparedStateFromBuffer(juce::AudioBuffer<float> buffer,
                                                         double sampleRate,
                                                         const juce::String& displayName,
                                                         const juce::String& path,
                                                         bool forceIdentityOrder)
{
  const auto totalSamples = static_cast<size_t>(buffer.getNumSamples());
  if (totalSamples == 0 || sampleRate <= 0)
    return;

  cutshuffle::CutShuffleEngine engine;
  engine.setBpm(static_cast<double>(*apvts.getRawParameterValue(kBpmId)));
  const int granIndex = juce::jlimit(
      0, kNumGranularityChoices - 1,
      static_cast<int>(std::round(apvts.getRawParameterValue(kGranularityId)->load())));
  const double beatsPerSlice = kBeatsPerSlice[granIndex];
  auto slices = engine.computeSlices(totalSamples, sampleRate, beatsPerSlice);
  if (slices.empty())
    return;

  std::vector<size_t> order;
  if (forceIdentityOrder)
  {
    order.resize(slices.size());
    std::iota(order.begin(), order.end(), size_t(0));
  }
  else
  {
    const uint32_t seed = static_cast<uint32_t>(*apvts.getRawParameterValue(kSeedId));
    if (seed == 0)
    {
      order.resize(slices.size());
      std::iota(order.begin(), order.end(), size_t(0));
    }
    else
    {
      order = cutshuffle::CutShuffleEngine::shuffledSliceOrder(slices.size(), seed, true);
    }
  }

  auto state = std::make_shared<PreparedState>();
  state->buffer = std::move(buffer);
  state->sampleRate = sampleRate;
  state->lengthInSamples = static_cast<juce::int64>(totalSamples);
  state->slices = std::move(slices);
  // If we are restoring from preset/state, we may have a pending playback order.
  // When forcing identity (e.g. after granularity/BPM change), we intentionally
  // ignore any pending order and reset arrangement.
  if (!forceIdentityOrder &&
      !pendingPlaybackOrder_.empty() &&
      pendingPlaybackOrder_.size() == state->slices.size())
  {
    state->playbackOrder = pendingPlaybackOrder_;
    pendingPlaybackOrder_.clear();
  }
  else
  {
    state->playbackOrder = std::move(order);
  }

  {
    std::unique_lock lock(stateMutex_);
    loadedSampleDisplayName_ = displayName;
    loadedSamplePath_ = path;
    loadErrorText_.clear();
  }

  applyNewPreparedState(std::move(state));
}

void CutShufflePluginProcessor::loadSampleFromFile(const juce::File& file)
{
  if (!file.existsAsFile())
  {
    setLoadStatus(LoadStatus::Error);
    setLoadError("File not found");
    return;
  }

  const uint64_t jobId = ++loadJobId_;
  setLoadStatus(LoadStatus::Loading);
  setLoadError({});

  loadPool_.addJob([this, file, jobId]()
  {
    auto loaded = loadWav(file);
    if (jobId != loadJobId_.load())
      return;

    if (!loaded)
    {
      juce::MessageManager::callAsync([this]()
      {
        setLoadStatus(LoadStatus::Error);
        setLoadError("Could not decode file");
      });
      return;
    }

    if (loaded->buffer.getNumSamples() == 0)
    {
      juce::MessageManager::callAsync([this]()
      {
        setLoadStatus(LoadStatus::Error);
        setLoadError("Empty file");
      });
      return;
    }

    const juce::String displayName = file.getFileName();
    const juce::String path = file.getFullPathName();
    juce::AudioBuffer<float> decodedBuffer = std::move(loaded->buffer);
    const double sr = loaded->sampleRate;

    juce::MessageManager::callAsync([this, buf = std::move(decodedBuffer), sr, displayName, path]() mutable
    {
      buildPreparedStateFromBuffer(std::move(buf), sr, displayName, path, false);
    });
  });
}

void CutShufflePluginProcessor::clearSample()
{
  ++loadJobId_;
  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_.reset();
  }
  setLoadStatus(LoadStatus::Idle);
  setLoadError({});
  {
    std::unique_lock lock(stateMutex_);
    loadedSampleDisplayName_.clear();
    loadedSamplePath_.clear();
  }
  for (auto& v : voices_)
    v.active = false;
}

void CutShufflePluginProcessor::regenerateSliceMap()
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || state->buffer.getNumSamples() == 0)
    return;
  juce::String displayName;
  juce::String path;
  {
    std::shared_lock lock(stateMutex_);
    displayName = loadedSampleDisplayName_;
    path = loadedSamplePath_;
  }
  // Changing granularity/BPM resets arrangement to identity on the new slice grid.
  buildPreparedStateFromBuffer(
      juce::AudioBuffer<float>(state->buffer), state->sampleRate, displayName, path, true);
}

void CutShufflePluginProcessor::rearrangeSample()
{
  rearrangeSample({});
}

void CutShufflePluginProcessor::rearrangeSample(const std::unordered_set<size_t>& selectedPositions)
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || state->buffer.getNumSamples() == 0)
    return;

  if (state->slices.empty())
    return;

  const size_t totalSlices = state->slices.size();
  if (state->playbackOrder.size() != totalSlices)
    return;

  if (!selectedPositions.empty())
  {
    // Only shuffle the selected logical positions; leave others unchanged.
    std::vector<size_t> positions(selectedPositions.begin(), selectedPositions.end());
    std::sort(positions.begin(), positions.end());
    // Remove any out-of-range
    positions.erase(std::remove_if(positions.begin(), positions.end(),
                                   [totalSlices](size_t p) { return p >= totalSlices; }),
                    positions.end());
    if (positions.size() <= 1)
      return;

    std::vector<size_t> physAtSelected;
    physAtSelected.reserve(positions.size());
    for (size_t p : positions)
      physAtSelected.push_back(state->playbackOrder[p]);

    const uint32_t seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt(1000000));
    const std::vector<size_t> shuffledOrder =
        cutshuffle::CutShuffleEngine::shuffledSliceOrder(physAtSelected.size(), seed, true);

    std::vector<size_t> newOrder = state->playbackOrder;
    for (size_t i = 0; i < positions.size(); ++i)
      newOrder[positions[i]] = physAtSelected[shuffledOrder[i]];

    auto newState = std::make_shared<PreparedState>(*state);
    newState->playbackOrder = std::move(newOrder);
    applyNewPreparedState(std::move(newState));
    apvts.getParameterAsValue(kSeedId).setValue(static_cast<int>(seed));
    return;
  }

  // No selection: shuffle within current window (existing behaviour)
  const auto [physStart, physEnd] = getWindowSliceRange(*state);
  if (physEnd <= physStart)
    return;

  // Collect physical slice indices that lie inside the window.
  std::vector<size_t> physInWindow;
  physInWindow.reserve(physEnd - physStart);
  for (size_t p = physStart; p < physEnd; ++p)
    physInWindow.push_back(p);

  // Collect logical positions whose current physical slice is in that window.
  std::vector<size_t> logicalPositions;
  logicalPositions.reserve(physInWindow.size());
  for (size_t logical = 0; logical < totalSlices; ++logical)
  {
    const size_t phys = state->playbackOrder[logical];
    if (phys >= physStart && phys < physEnd)
      logicalPositions.push_back(logical);
  }

  if (logicalPositions.size() <= 1 || physInWindow.empty())
    return;

  const uint32_t seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt(1000000));
  const std::vector<size_t> order =
      cutshuffle::CutShuffleEngine::shuffledSliceOrder(physInWindow.size(), seed, true);

  // Non-destructive: shuffle only the set of physical slices that belong to
  // the window, and only at the logical positions currently mapped to them.
  std::vector<size_t> newOrder = state->playbackOrder;
  const size_t count = std::min(logicalPositions.size(), physInWindow.size());
  for (size_t i = 0; i < count; ++i)
  {
    const size_t dstLogical = logicalPositions[i];
    const size_t srcPhys = physInWindow[order[i]];
    newOrder[dstLogical] = srcPhys;
  }

  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(newOrder);

  applyNewPreparedState(std::move(newState));

  apvts.getParameterAsValue(kSeedId).setValue(static_cast<int>(seed));
}

std::vector<size_t> CutShufflePluginProcessor::moveSelectedSlicesInOrder(
    const std::unordered_set<size_t>& selectedPositions, int direction)
{
  if (selectedPositions.empty() || (direction != -1 && direction != 1))
    return {};

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || state->slices.empty() || state->playbackOrder.size() != state->slices.size())
    return {};

  std::vector<size_t> order = state->playbackOrder;
  const size_t n = order.size();
  const std::vector<cutshuffle::Slice>& slices = state->slices;

  // selectedPositions = the visual positions (slice indices) the user clicked
  std::vector<size_t> positions;
  for (size_t i = 0; i < n; ++i)
    if (selectedPositions.count(i))
      positions.push_back(i);

  if (positions.empty())
    return {};

  // Group positions into runs of consecutive indices (e.g. [1,2,5] -> runs [1,2] and [5])
  // For each run we rotate the segment so only those slices move (no trail of copies)
  std::vector<std::pair<size_t, size_t>> runs; // [start, end] inclusive
  for (size_t p : positions)
  {
    if (!runs.empty() && runs.back().second + 1 == p)
      runs.back().second = p;
    else
      runs.push_back({p, p});
  }

  for (const auto& [runStart, runEnd] : runs)
  {
    if (direction > 0)
    {
      const size_t segEnd = runEnd + 1; // segment [runStart, segEnd] rotates right
      if (segEnd >= n)
        continue;
      const size_t segLen = segEnd - runStart + 1;
      bool sameLength = true;
      for (size_t i = 0; i < segLen - 1 && sameLength; ++i)
        if (slices[runStart + i].lengthSamples != slices[runStart + i + 1].lengthSamples)
          sameLength = false;

      if (!sameLength && segLen != 2)
        continue;

      std::vector<size_t> oldOrderSeg(segLen);
      for (size_t i = 0; i < segLen; ++i)
        oldOrderSeg[i] = order[runStart + i];
      for (size_t i = 0; i < segLen; ++i)
        order[runStart + (i + 1) % segLen] = oldOrderSeg[i];

    }
    else
    {
      if (runStart == 0)
        continue;
      const size_t segStart = runStart - 1;
      const size_t segLen = runEnd - segStart + 1;
      bool sameLength = true;
      for (size_t i = 0; i < segLen - 1 && sameLength; ++i)
        if (slices[segStart + i].lengthSamples != slices[segStart + i + 1].lengthSamples)
          sameLength = false;

      if (!sameLength && segLen != 2)
        continue;

      std::vector<size_t> oldOrderSeg(segLen);
      for (size_t i = 0; i < segLen; ++i)
        oldOrderSeg[i] = order[segStart + i];
      for (size_t i = 0; i < segLen; ++i)
        order[segStart + (i + segLen - 1) % segLen] = oldOrderSeg[i];

    }
  }

  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = order;

  applyNewPreparedState(std::move(newState));
  return order;
}

std::pair<juce::int64, juce::int64> CutShufflePluginProcessor::getWindowRangeSnappedToSlices() const
{
  auto state = getPreparedState();
  if (!state)
    return {0, 0};
  return getWindowRangeSnappedToSlices(*state);
}

std::pair<juce::int64, juce::int64> CutShufflePluginProcessor::getWindowRangeSnappedToSlices(
    const PreparedState& state) const
{
  const juce::int64 totalSamples = state.lengthInSamples;
  if (totalSamples <= 0)
    return {0, 0};

  const std::vector<cutshuffle::Slice>& slices = state.slices;
  if (slices.empty())
    return {0, totalSamples};

  const int windowParam = juce::jlimit(
      4, 64,
      static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
  const size_t windowSlices = static_cast<size_t>(juce::jlimit(4, 64, (windowParam + 1) & ~1)); // even: 4, 6, 8, ...
  const size_t numSlices = slices.size();
  if (windowSlices >= numSlices)
    return {0, totalSamples};

  const float posNorm = apvts.getRawParameterValue(kWindowPositionId)->load();
  const size_t maxStart = numSlices - windowSlices;
  const size_t startIdx = std::min(
      maxStart,
      static_cast<size_t>(std::round(static_cast<double>(posNorm) * static_cast<double>(maxStart))));

  const cutshuffle::Slice& first = slices[startIdx];
  const cutshuffle::Slice& last = slices[startIdx + windowSlices - 1];
  const juce::int64 startSnap = static_cast<juce::int64>(first.startSample);
  const juce::int64 endSnap = static_cast<juce::int64>(last.startSample) + static_cast<juce::int64>(last.lengthSamples);

  return {startSnap, juce::jmin(endSnap, totalSamples)};
}

void CutShufflePluginProcessor::startPreview()
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  previewPlaybackPos_.store(0.f);
  if (!state || state->buffer.getNumSamples() == 0 || state->sampleRate <= 0)
  {
    previewLengthSamples_.store(0);
    previewActive_.store(true);
    return;
  }

  // Render current window (respecting playbackOrder) into preview buffer at file sample rate.
  {
    renderWindowToBuffer(*state, previewBuffer_);
  }
  const juce::int64 totalPreviewSamples = previewBuffer_.getNumSamples();
  // Cap at 5 seconds in source (file) time so playback duration matches export.
  const juce::int64 maxPreviewSourceSamples =
      static_cast<juce::int64>(kPreviewLengthSeconds * state->sampleRate);
  previewLengthSamples_.store(juce::jmin(totalPreviewSamples, maxPreviewSourceSamples));
  previewSourceToHostRatio_.store(
      static_cast<float>(state->sampleRate / hostSampleRate_));
  previewActive_.store(true);
}

void CutShufflePluginProcessor::stopPreview()
{
  previewActive_.store(false);
}

bool CutShufflePluginProcessor::isPreviewActive() const
{
  return previewActive_.load();
}

void CutShufflePluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
  juce::ScopedNoDenormals noDenormals;
  buffer.clear();

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }

  // Preview: play pre-rendered window (respecting playbackOrder), resampling file rate -> host rate
  if (previewActive_.load() && previewBuffer_.getNumSamples() > 0)
  {
    const int numSrcCh = previewBuffer_.getNumChannels();
    const int totalSamples = previewBuffer_.getNumSamples();
    const juce::int64 lenS = previewLengthSamples_.load();
    const int blockSamples = buffer.getNumSamples();
    const int numOutCh = buffer.getNumChannels();
    const float ratio = previewSourceToHostRatio_.load();
    float pos = previewPlaybackPos_.load();

    for (int i = 0; i < blockSamples; ++i)
    {
      if (lenS <= 0 || pos >= static_cast<float>(lenS))
      {
        previewActive_.store(false);
        break;
      }
      const int i0 = static_cast<int>(pos);
      const int i1 = juce::jmin(i0 + 1, totalSamples - 1);
      const float frac = pos - static_cast<float>(i0);
      for (int ch = 0; ch < numOutCh; ++ch)
      {
        const int srcCh = std::min(ch, numSrcCh - 1);
        const float s0 = previewBuffer_.getSample(srcCh, i0);
        const float s1 = previewBuffer_.getSample(srcCh, i1);
        buffer.addSample(ch, i, s0 + frac * (s1 - s0));
      }
      pos += ratio;
    }
    previewPlaybackPos_.store(pos);
    if (lenS > 0 && pos >= static_cast<float>(lenS))
      previewActive_.store(false);
    return;
  }

  if (!state || state->slices.empty())
    return;

  const int rootNote = 36; // C1 = slice 0
  const int polyphony = 8;
  const int numOutCh = buffer.getNumChannels();
  const int numSrcCh = state->buffer.getNumChannels();
  const size_t numSlices = state->slices.size();

  // Handle MIDI
  for (const auto midi : midiMessages)
  {
    const auto msg = midi.getMessage();
    if (msg.isNoteOn())
    {
      const int midiNote = msg.getNoteNumber();
      const int sliceIndex = midiNote - rootNote;
      if (sliceIndex < 0 || sliceIndex >= static_cast<int>(numSlices))
        continue;

      lastTriggeredSliceIndex.store(sliceIndex);

      const size_t orderIndex = static_cast<size_t>(sliceIndex);
      const size_t srcSliceIdx = orderIndex < state->playbackOrder.size()
          ? state->playbackOrder[orderIndex]
          : orderIndex;
      if (srcSliceIdx >= state->slices.size())
        continue;

      const float vel = msg.getFloatVelocity();

      // Find free voice or steal (respect polyphony)
      int vIdx = -1;
      int activeCount = 0;
      for (size_t i = 0; i < voices_.size(); ++i)
      {
        if (!voices_[i].active)
        {
          if (vIdx < 0)
            vIdx = static_cast<int>(i);
        }
        else
          ++activeCount;
      }
      if (vIdx < 0 && activeCount >= polyphony)
      {
        // Steal first active voice
        for (size_t i = 0; i < voices_.size(); ++i)
        {
          if (voices_[i].active)
          {
            vIdx = static_cast<int>(i);
            break;
          }
        }
      }
      if (vIdx < 0)
        vIdx = 0;

      Voice& v = voices_[static_cast<size_t>(vIdx)];
      v.sliceIndex = static_cast<int>(srcSliceIdx);
      v.samplePosition = 0;
      v.active = true;
      v.noteId = msg.getNoteNumber();
      v.gain = vel;
    }
  }

  midiMessages.clear();

  // Render voices
  const int blockSamples = buffer.getNumSamples();
  for (auto& v : voices_)
  {
    if (!v.active || v.sliceIndex < 0 || v.sliceIndex >= static_cast<int>(state->slices.size()))
      continue;

    const cutshuffle::Slice& sl = state->slices[static_cast<size_t>(v.sliceIndex)];
    const size_t sliceLen = sl.lengthSamples;
    const size_t start = sl.startSample;

    for (int i = 0; i < blockSamples; ++i)
    {
      if (v.samplePosition >= sliceLen)
      {
        v.active = false;
        break;
      }

      const size_t srcSample = start + v.samplePosition;
      for (int ch = 0; ch < numOutCh; ++ch)
      {
        const int srcCh = std::min(ch, numSrcCh - 1);
        const float sample = v.gain * state->buffer.getSample(srcCh, static_cast<int>(srcSample));
        buffer.addSample(ch, i, sample);
      }
      ++v.samplePosition;
    }
  }
}

bool CutShufflePluginProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* CutShufflePluginProcessor::createEditor()
{
  return new CutShufflePluginEditor(*this);
}

void CutShufflePluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
  auto xml = createFullStateXml();
  if (xml)
    copyXmlToBinary(*xml, destData);
}

void CutShufflePluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml)
  {
    // Session/host restore: restore params but do not auto-load the last sample.
    restoreStateFromXml(*xml, false);
    // Reset waveform to identity order when restoring from memory (startup/session).
    resetArrangement();
  }
}

std::unique_ptr<juce::XmlElement> CutShufflePluginProcessor::createFullStateXml() const
{
  auto vt = const_cast<juce::AudioProcessorValueTreeState&>(apvts).copyState();
  std::unique_ptr<juce::XmlElement> xml(vt.createXml());
  if (!xml)
    return nullptr;

  // Version for future-proofing
  xml->setAttribute(kStateVersionAttr, kCurrentStateVersion);

  // Persist the sample path (path-based loading strategy)
  {
    std::shared_lock lock(stateMutex_);
    xml->setAttribute(kSamplePathAttr, loadedSamplePath_);
  }

  // Persist playback order so manual slice reordering survives reloads
  if (auto prepared = getPreparedState())
  {
    if (!prepared->playbackOrder.empty())
    {
      juce::String orderString;
      const size_t n = prepared->playbackOrder.size();
      for (size_t i = 0; i < n; ++i)
      {
        if (i > 0)
          orderString << ",";
        orderString << static_cast<int>(prepared->playbackOrder[i]);
      }
      xml->setAttribute(kPlaybackOrderAttr, orderString);
    }
  }

  return xml;
}

void CutShufflePluginProcessor::restoreStateFromXml(const juce::XmlElement& xml, bool restoreSamplePath)
{
  // Restore APVTS (all parameters)
  apvts.replaceState(juce::ValueTree::fromXml(xml));

  // Versioning hook (currently unused but kept for future migrations)
  const int version = xml.getIntAttribute(kStateVersionAttr, 0);
  juce::ignoreUnused(version);

  // Restore any saved playback order so it can be applied after sample load
  pendingPlaybackOrder_.clear();
  const juce::String playbackOrderStr = xml.getStringAttribute(kPlaybackOrderAttr, {});
  if (playbackOrderStr.isNotEmpty())
  {
    juce::StringArray tokens;
    tokens.addTokens(playbackOrderStr, ",", "");
    tokens.removeEmptyStrings();
    pendingPlaybackOrder_.reserve(static_cast<size_t>(tokens.size()));
    for (const auto& t : tokens)
    {
      const int v = t.getIntValue();
      if (v >= 0)
        pendingPlaybackOrder_.push_back(static_cast<size_t>(v));
    }
  }

  if (!restoreSamplePath)
  {
    // Session restore: start with no sample loaded, default window size and granularity.
    clearSample();
    if (auto* param = apvts.getParameter(kWindowBeatsId))
    {
      // Default window = 16 slices; normalized = (16 - 4) / (64 - 4)
      param->setValueNotifyingHost(12.0f / 60.0f);
    }
    if (auto* param = apvts.getParameter(kGranularityId))
    {
      // Default granularity = 1/2 beat (index 1); range is 0..4
      param->setValueNotifyingHost(1.0f / 4.0f); // normalized: index 1 of 5 = 1/4
    }
    return;
  }

  // Preset load: restore sample path and kick off async load (or mark Missing)
  const juce::String path = xml.getStringAttribute(kSamplePathAttr, {});
  if (path.isEmpty())
    return;

  juce::File f(path);
  if (f.existsAsFile())
  {
    loadSampleFromFile(f);
  }
  else
  {
    std::unique_lock lock(stateMutex_);
    loadedSamplePath_ = path;
    loadedSampleDisplayName_ = f.getFileName();
    setLoadStatus(LoadStatus::Missing);
  }
}

bool CutShufflePluginProcessor::savePresetToFile(const juce::File& file) const
{
  auto xml = createFullStateXml();
  if (!xml)
    return false;

  juce::FileOutputStream out(file);
  if (!out.openedOk())
    return false;

  out.setNewLineString("\n");
  out << xml->toString();
  out.flush();
  return true;
}

bool CutShufflePluginProcessor::loadPresetFromFile(const juce::File& file)
{
  if (!file.existsAsFile())
    return false;

  juce::XmlDocument doc(file);
  std::unique_ptr<juce::XmlElement> xml(doc.getDocumentElement());
  if (!xml)
    return false;

  loadingPreset_.store(true);
  restoreStateFromXml(*xml, true);
  loadingPreset_.store(false);
  return true;
}

void CutShufflePluginProcessor::resetArrangement()
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || state->slices.empty())
    return;

  const size_t n = state->slices.size();
  std::vector<size_t> identityOrder(n);
  std::iota(identityOrder.begin(), identityOrder.end(), size_t(0));

  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(identityOrder);

  applyNewPreparedState(std::move(newState));
}

std::pair<size_t, size_t> CutShufflePluginProcessor::getWindowSliceRange(const PreparedState& state) const
{
  auto [startSample, endSample] = getWindowRangeSnappedToSlices(state);
  if (endSample <= startSample)
    return {0, 0};

  const std::vector<cutshuffle::Slice>& slices = state.slices;
  const size_t numSlices = slices.size();
  size_t firstSlice = numSlices;
  size_t lastSlice = 0;
  for (size_t i = 0; i < numSlices; ++i)
  {
    const auto& sl = slices[i];
    const juce::int64 slStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 slEnd = slStart + static_cast<juce::int64>(sl.lengthSamples);
    if (slEnd <= startSample || slStart >= endSample)
      continue;
    firstSlice = std::min(firstSlice, i);
    lastSlice = std::max(lastSlice, i);
  }

  if (firstSlice == numSlices || lastSlice < firstSlice)
    return {0, 0};

  return {firstSlice, lastSlice + 1}; // [start, end)
}

void CutShufflePluginProcessor::renderWindowToBuffer(const PreparedState& state,
                                                 juce::AudioBuffer<float>& out) const
{
  const auto [physStart, physEnd] = getWindowSliceRange(state);
  if (physEnd <= physStart)
  {
    out.setSize(0, 0);
    return;
  }

  const size_t totalSlices = state.slices.size();
  if (state.playbackOrder.size() != totalSlices)
  {
    out.setSize(0, 0);
    return;
  }

  const int numCh = state.buffer.getNumChannels();
  size_t totalSamples = 0;

  // First pass: find logical positions whose physical slice lies in the window.
  std::vector<size_t> logicalPositions;
  logicalPositions.reserve(totalSlices);
  for (size_t logical = 0; logical < totalSlices; ++logical)
  {
    const size_t srcIdx = state.playbackOrder[logical];
    if (srcIdx < physStart || srcIdx >= physEnd)
      continue;
    logicalPositions.push_back(logical);
    totalSamples += state.slices[srcIdx].lengthSamples;
  }

  if (totalSamples == 0)
  {
    out.setSize(0, 0);
    return;
  }

  out.setSize(numCh, static_cast<int>(totalSamples), false, false, true);
  out.clear();

  size_t writePos = 0;
  for (size_t logical : logicalPositions)
  {
    const size_t srcIdx = state.playbackOrder[logical];
    if (srcIdx >= totalSlices)
      continue;
    const auto& sl = state.slices[srcIdx];
    const int len = static_cast<int>(sl.lengthSamples);
    const int srcStart = static_cast<int>(sl.startSample);
    for (int ch = 0; ch < numCh; ++ch)
    {
      out.copyFrom(ch,
                   static_cast<int>(writePos),
                   state.buffer,
                   ch,
                   srcStart,
                   len);
    }
    writePos += static_cast<size_t>(len);
  }
}

void CutShufflePluginProcessor::renderSampleRangeToBuffer(const PreparedState& state,
                                                         juce::int64 startSample,
                                                         juce::int64 endSample,
                                                         juce::AudioBuffer<float>& out) const
{
  if (startSample >= endSample)
  {
    out.setSize(0, 0);
    return;
  }
  const size_t totalSlices = state.slices.size();
  if (state.playbackOrder.size() != totalSlices)
  {
    out.setSize(0, 0);
    return;
  }
  const int numCh = state.buffer.getNumChannels();
  const juce::int64 totalLength = state.lengthInSamples;
  startSample = juce::jlimit(juce::int64(0), totalLength, startSample);
  endSample = juce::jlimit(juce::int64(0), totalLength, endSample);
  if (endSample <= startSample)
  {
    out.setSize(0, 0);
    return;
  }

  // First pass: compute output length (sum of overlapping portions in playback order)
  size_t outSamples = 0;
  for (size_t logical = 0; logical < totalSlices; ++logical)
  {
    const size_t srcIdx = state.playbackOrder[logical];
    if (srcIdx >= totalSlices)
      continue;
    const auto& sl = state.slices[srcIdx];
    const juce::int64 sliceStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 sliceEnd = sliceStart + static_cast<juce::int64>(sl.lengthSamples);
    const juce::int64 overlapStart = juce::jmax(sliceStart, startSample);
    const juce::int64 overlapEnd = juce::jmin(sliceEnd, endSample);
    if (overlapStart < overlapEnd)
      outSamples += static_cast<size_t>(overlapEnd - overlapStart);
  }

  if (outSamples == 0)
  {
    out.setSize(0, 0);
    return;
  }

  out.setSize(numCh, static_cast<int>(outSamples), false, false, true);
  out.clear();

  size_t writePos = 0;
  for (size_t logical = 0; logical < totalSlices; ++logical)
  {
    const size_t srcIdx = state.playbackOrder[logical];
    if (srcIdx >= totalSlices)
      continue;
    const auto& sl = state.slices[srcIdx];
    const juce::int64 sliceStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 sliceEnd = sliceStart + static_cast<juce::int64>(sl.lengthSamples);
    const juce::int64 overlapStart = juce::jmax(sliceStart, startSample);
    const juce::int64 overlapEnd = juce::jmin(sliceEnd, endSample);
    if (overlapStart >= overlapEnd)
      continue;

    const int srcStart = static_cast<int>(overlapStart);
    const int len = static_cast<int>(overlapEnd - overlapStart);
    for (int ch = 0; ch < numCh; ++ch)
    {
      out.copyFrom(ch,
                   static_cast<int>(writePos),
                   state.buffer,
                   ch,
                   srcStart,
                   len);
    }
    writePos += static_cast<size_t>(len);
  }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
  return static_cast<juce::AudioProcessor*>(new CutShufflePluginProcessor());
}
