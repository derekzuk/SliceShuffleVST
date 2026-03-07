#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../cli/WavLoader.h"
#include "../cli/BpmDetector.h"
#include "../dsp/SliceShuffleEngine.h"
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
constexpr const char* kFileNameAttr = "fileName";
constexpr const char* kFileSizeAttr = "fileSize";
constexpr const char* kFileLastModifiedAttr = "fileLastModified";
constexpr const char* kEditorWidthAttr = "editorW";
constexpr const char* kEditorHeightAttr = "editorH";
constexpr const char* kPlaybackOrderAttr = "playbackOrder";
constexpr const char* kMutedPositionsAttr = "mutedPositions";
constexpr const char* kReversedPositionsAttr = "reversedPositions";

constexpr int kCurrentStateVersion = 1;

// Micro fade at slice boundaries to avoid clicks (0.5–5 ms typical; 2 ms is a good default)
constexpr double kSliceFadeMs = 2.0;

/** Apply linear fade-in to first fadeInSamples and fade-out to last fadeOutSamples in buf. */
void applySliceFades(juce::AudioBuffer<float>& buf, int startSample, int len,
                    int fadeInSamples, int fadeOutSamples)
{
  if (len <= 0)
    return;
  fadeInSamples = juce::jmin(fadeInSamples, len);
  fadeOutSamples = juce::jmin(fadeOutSamples, len);
  const int numCh = buf.getNumChannels();
  for (int ch = 0; ch < numCh; ++ch)
  {
    float* data = buf.getWritePointer(ch);
    for (int i = 0; i < fadeInSamples; ++i)
      data[startSample + i] *= (i + 1) / static_cast<float>(fadeInSamples);
    for (int i = 0; i < fadeOutSamples; ++i)
    {
      const int idx = startSample + len - 1 - i;
      data[idx] *= (i + 1) / static_cast<float>(fadeOutSamples);
    }
  }
}

/** Same as above but only apply fade-in and/or fade-out when the corresponding flag is true.
 *  Use when playing in original order: skip fades at boundaries that are continuous in the source. */
void applySliceFades(juce::AudioBuffer<float>& buf, int startSample, int len,
                    int fadeInSamples, int fadeOutSamples, bool applyFadeIn, bool applyFadeOut)
{
  if (len <= 0)
    return;
  if (!applyFadeIn && !applyFadeOut)
    return;
  fadeInSamples = applyFadeIn ? juce::jmin(fadeInSamples, len) : 0;
  fadeOutSamples = applyFadeOut ? juce::jmin(fadeOutSamples, len) : 0;
  const int numCh = buf.getNumChannels();
  for (int ch = 0; ch < numCh; ++ch)
  {
    float* data = buf.getWritePointer(ch);
    if (fadeInSamples > 0)
      for (int i = 0; i < fadeInSamples; ++i)
        data[startSample + i] *= (i + 1) / static_cast<float>(fadeInSamples);
    if (fadeOutSamples > 0)
      for (int i = 0; i < fadeOutSamples; ++i)
      {
        const int idx = startSample + len - 1 - i;
        data[idx] *= (i + 1) / static_cast<float>(fadeOutSamples);
      }
  }
}

/** Fade length in samples for a slice; clamped to half slice length. */
int fadeSamplesForSlice(double sampleRate, size_t sliceLen)
{
  const int n = static_cast<int>(kSliceFadeMs * sampleRate / 1000.0);
  return juce::jlimit(0, static_cast<int>(sliceLen / 2), n);
}

/** Apply fades to a segment that may be a partial slice (for renderSampleRangeToBuffer).
 *  Only applies fade-in/fade-out when the boundary is a discontinuity in playback order. */
void applySliceFadesSegment(juce::AudioBuffer<float>& buf, int startSample, int len,
                            size_t offsetInSliceStart, size_t sliceLen, int fadeSamples,
                            bool applyFadeInAtStart, bool applyFadeOutAtEnd)
{
  if (len <= 0 || fadeSamples <= 0)
    return;
  if (!applyFadeInAtStart && !applyFadeOutAtEnd)
    return;
  const int numCh = buf.getNumChannels();
  for (int ch = 0; ch < numCh; ++ch)
  {
    float* data = buf.getWritePointer(ch);
    for (int i = 0; i < len; ++i)
    {
      const size_t posInSlice = offsetInSliceStart + static_cast<size_t>(i);
      float gain = 1.0f;
      if (applyFadeInAtStart && posInSlice < static_cast<size_t>(fadeSamples))
        gain = (static_cast<float>(posInSlice) + 1.0f) / static_cast<float>(fadeSamples);
      else if (applyFadeOutAtEnd && posInSlice >= sliceLen - static_cast<size_t>(fadeSamples) && sliceLen > static_cast<size_t>(fadeSamples))
        gain = static_cast<float>(sliceLen - posInSlice) / static_cast<float>(fadeSamples);
      data[startSample + i] *= gain;
    }
  }
}

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
      juce::StringArray{"1/4", "1/2", "1", "2", "4"}, 2)); // index 2 = "1" beat default
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kSeedId, 1}, "Seed", 0, 999999, 0));
  // Window = number of slices (min 4); we use even count only (4, 6, 8, …)
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kWindowBeatsId, 1}, "Window", 4, 64, 4));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kWindowPositionId, 1}, "Window position",
      juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f), 0.f));
  return layout;
}
} // namespace

SliceShufflePluginProcessor::SliceShufflePluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(
        BusesProperties()
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "SliceShuffleParams", createParameterLayout())
#endif
{
#ifdef JucePlugin_PreferredChannelConfigurations
  apvts = juce::AudioProcessorValueTreeState(*this, nullptr, "SliceShuffleParams", createParameterLayout());
#endif
  voices_.resize(kMaxVoices);
}

SliceShufflePluginProcessor::~SliceShufflePluginProcessor() = default;

const juce::String SliceShufflePluginProcessor::getName() const { return JucePlugin_Name; }

bool SliceShufflePluginProcessor::acceptsMidi() const { return true; }
bool SliceShufflePluginProcessor::producesMidi() const { return false; }
bool SliceShufflePluginProcessor::isMidiEffect() const { return false; }
double SliceShufflePluginProcessor::getTailLengthSeconds() const { return 0.0; }

int SliceShufflePluginProcessor::getNumPrograms() { return 1; }
int SliceShufflePluginProcessor::getCurrentProgram() { return 0; }
void SliceShufflePluginProcessor::setCurrentProgram(int) {}
const juce::String SliceShufflePluginProcessor::getProgramName(int) { return {}; }
void SliceShufflePluginProcessor::changeProgramName(int, const juce::String&) {}

void SliceShufflePluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  hostSampleRate_ = sampleRate;
  hostBlockSize_ = samplesPerBlock;
}

void SliceShufflePluginProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SliceShufflePluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
      || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

std::shared_ptr<const PreparedState> SliceShufflePluginProcessor::getPreparedState() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_;
}

SliceShufflePluginProcessor::LoadStatus SliceShufflePluginProcessor::getLoadStatus() const
{
  return loadStatus_.load();
}

juce::String SliceShufflePluginProcessor::getLoadedSampleDisplayName() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSampleDisplayName_;
}

juce::String SliceShufflePluginProcessor::getLoadedSamplePath() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSamplePath_;
}

int SliceShufflePluginProcessor::getNumSlices() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_ ? static_cast<int>(preparedState_->slices.size()) : 0;
}

juce::String SliceShufflePluginProcessor::getLoadErrorText() const
{
  std::shared_lock lock(stateMutex_);
  return loadErrorText_;
}

void SliceShufflePluginProcessor::setLoadStatus(LoadStatus s)
{
  loadStatus_.store(s);
}

void SliceShufflePluginProcessor::setLoadError(const juce::String& text)
{
  std::unique_lock lock(stateMutex_);
  loadErrorText_ = text;
}

void SliceShufflePluginProcessor::applyNewPreparedState(std::shared_ptr<const PreparedState> state)
{
  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_ = std::move(state);
  }
  setLoadStatus(state ? LoadStatus::Ready : LoadStatus::Idle);
  if (!state)
    setLoadError({});
}

void SliceShufflePluginProcessor::pushUndoEntry(std::vector<size_t> currentOrder,
                                             std::optional<std::unordered_set<size_t>> selectionToRestore,
                                             std::optional<std::unordered_set<size_t>> mutedToRestore,
                                             std::optional<std::unordered_set<size_t>> reversedToRestore)
{
  undo_.push_back({gen_, std::move(currentOrder), std::move(selectionToRestore), std::move(mutedToRestore), std::move(reversedToRestore)});
  redo_.clear();
  while (undo_.size() > kMaxUndoSteps)
    undo_.pop_front();
}

void SliceShufflePluginProcessor::incrementGeneration()
{
  ++gen_;
}

void SliceShufflePluginProcessor::undo(const std::unordered_set<size_t>& currentSelection)
{
  while (!undo_.empty() && undo_.back().gen != gen_)
    undo_.pop_back();
  if (undo_.empty())
    return;
  UndoEntry entry = std::move(undo_.back());
  undo_.pop_back();

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || entry.order.empty())
    return;
  // Allow restoring order of different length (e.g. undo after delete).
  for (size_t idx : entry.order)
    if (idx >= state->slices.size())
      return;
  redo_.push_back({gen_, state->playbackOrder, currentSelection, state->mutedLogicalPositions, state->reversedLogicalPositions});

  if (entry.selectionToRestore)
    pendingRestoreSelection_ = entry.selectionToRestore;

  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(entry.order);
  if (entry.mutedToRestore)
    newState->mutedLogicalPositions = *entry.mutedToRestore;
  if (entry.reversedToRestore)
    newState->reversedLogicalPositions = *entry.reversedToRestore;
  applyNewPreparedState(std::move(newState));
}

void SliceShufflePluginProcessor::redo(const std::unordered_set<size_t>& currentSelection)
{
  while (!redo_.empty() && redo_.back().gen != gen_)
    redo_.pop_back();
  if (redo_.empty())
    return;
  UndoEntry entry = std::move(redo_.back());
  redo_.pop_back();

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || entry.order.empty())
    return;
  for (size_t idx : entry.order)
    if (idx >= state->slices.size())
      return;
  undo_.push_back({gen_, state->playbackOrder, currentSelection, state->mutedLogicalPositions, state->reversedLogicalPositions});

  if (entry.selectionToRestore)
    pendingRestoreSelection_ = entry.selectionToRestore;

  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(entry.order);
  if (entry.mutedToRestore)
    newState->mutedLogicalPositions = *entry.mutedToRestore;
  if (entry.reversedToRestore)
    newState->reversedLogicalPositions = *entry.reversedToRestore;
  applyNewPreparedState(std::move(newState));
}

std::optional<std::unordered_set<size_t>> SliceShufflePluginProcessor::takePendingRestoreSelection()
{
  auto out = std::move(pendingRestoreSelection_);
  pendingRestoreSelection_ = std::nullopt;
  return out;
}

bool SliceShufflePluginProcessor::canUndo() const
{
  for (auto it = undo_.rbegin(); it != undo_.rend(); ++it)
    if (it->gen == gen_)
      return true;
  return false;
}

bool SliceShufflePluginProcessor::canRedo() const
{
  for (auto it = redo_.rbegin(); it != redo_.rend(); ++it)
    if (it->gen == gen_)
      return true;
  return false;
}

void SliceShufflePluginProcessor::buildPreparedStateFromBuffer(juce::AudioBuffer<float> buffer,
                                                         double sampleRate,
                                                         const juce::String& displayName,
                                                         const juce::String& path,
                                                         bool forceIdentityOrder)
{
  const auto totalSamples = static_cast<size_t>(buffer.getNumSamples());
  if (totalSamples == 0 || sampleRate <= 0)
    return;

  sliceshuffle::SliceShuffleEngine engine;
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
      order = sliceshuffle::SliceShuffleEngine::shuffledSliceOrder(slices.size(), seed, true);
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
  // Allow pending order of same or shorter length (e.g. after delete).
  if (!forceIdentityOrder && !pendingPlaybackOrder_.empty() &&
      pendingPlaybackOrder_.size() <= state->slices.size())
  {
    bool valid = true;
    for (size_t idx : pendingPlaybackOrder_)
      if (idx >= state->slices.size())
      {
        valid = false;
        break;
      }
    if (valid)
    {
      state->playbackOrder = pendingPlaybackOrder_;
      const size_t n = state->playbackOrder.size();
      for (size_t pos : pendingMutedLogicalPositions_)
        if (pos < n)
          state->mutedLogicalPositions.insert(pos);
      for (size_t pos : pendingReversedLogicalPositions_)
        if (pos < n)
          state->reversedLogicalPositions.insert(pos);
    }
    pendingPlaybackOrder_.clear();
    pendingMutedLogicalPositions_.clear();
    pendingReversedLogicalPositions_.clear();
    if (!valid)
      state->playbackOrder = std::move(order);
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
  incrementGeneration();
}

void SliceShufflePluginProcessor::loadSampleFromFile(const juce::File& file, bool restoreArrangement)
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

  loadPool_.addJob([this, file, jobId, restoreArrangement]()
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

    std::optional<double> detectedBpm;
    if (!restoreArrangement)
      detectedBpm = detectBpm(decodedBuffer, sr);

    juce::MessageManager::callAsync([this, buf = std::move(decodedBuffer), sr, displayName, path, restoreArrangement, detectedBpm]() mutable
    {
      // When restoring preset/state, use saved arrangement (forceIdentityOrder = false). Otherwise start with identity order.
      buildPreparedStateFromBuffer(std::move(buf), sr, displayName, path, !restoreArrangement);
      if (!restoreArrangement)
      {
        if (auto* param = apvts.getParameter(kWindowPositionId))
          param->setValueNotifyingHost(0.0f);
        if (detectedBpm && *detectedBpm > 0)
        {
          if (auto* bpmParam = apvts.getParameter(kBpmId))
          {
            const float bpmClamped = static_cast<float>(std::clamp(*detectedBpm, 40.0, 240.0));
            const float norm = (bpmClamped - 40.f) / 200.f;
            bpmParam->setValueNotifyingHost(norm);
          }
        }
      }
    });
  });
}

void SliceShufflePluginProcessor::clearSample()
{
  ++loadJobId_;
  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_.reset();
  }
  undo_.clear();
  redo_.clear();
  incrementGeneration();
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

void SliceShufflePluginProcessor::regenerateSliceMap()
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

void SliceShufflePluginProcessor::rearrangeSample()
{
  rearrangeSample({});
}

void SliceShufflePluginProcessor::rearrangeSample(const std::unordered_set<size_t>& selectedPositions)
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
  const size_t numPositions = state->playbackOrder.size();
  if (numPositions == 0)
    return;

  if (!selectedPositions.empty())
  {
    // Only shuffle the selected logical positions; leave others unchanged.
    std::vector<size_t> positions(selectedPositions.begin(), selectedPositions.end());
    std::sort(positions.begin(), positions.end());
    // Remove any out-of-range (use numPositions so shuffle works after delete too).
    positions.erase(std::remove_if(positions.begin(), positions.end(),
                                   [numPositions](size_t p) { return p >= numPositions; }),
                    positions.end());
    if (positions.size() <= 1)
      return;

    std::vector<size_t> physAtSelected;
    physAtSelected.reserve(positions.size());
    for (size_t p : positions)
      physAtSelected.push_back(state->playbackOrder[p]);

    // Capture mute flags for the selected positions so we can permute them
    // alongside the slices (muted "chips" should move with the shuffle).
    std::vector<bool> mutedFlags;
    mutedFlags.reserve(positions.size());
    for (size_t p : positions)
      mutedFlags.push_back(state->mutedLogicalPositions.count(p) != 0);

    const uint32_t seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt(1000000));
    const std::vector<size_t> shuffledOrder =
        sliceshuffle::SliceShuffleEngine::shuffledSliceOrder(physAtSelected.size(), seed, true);

    std::vector<size_t> newOrder = state->playbackOrder;
    for (size_t i = 0; i < positions.size(); ++i)
      newOrder[positions[i]] = physAtSelected[shuffledOrder[i]];

    // Build new muted set: start from current, then permute flags only within "positions".
    std::unordered_set<size_t> newMuted = state->mutedLogicalPositions;
    // Clear all mute flags at the affected positions.
    for (size_t p : positions)
      newMuted.erase(p);
    // Reapply flags according to the same permutation used for slices.
    for (size_t i = 0; i < positions.size(); ++i)
    {
      const bool wasMuted = mutedFlags[shuffledOrder[i]];
      if (wasMuted)
        newMuted.insert(positions[i]);
    }

    if (newOrder == state->playbackOrder && newMuted == state->mutedLogicalPositions)
      return;

    pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
    auto newState = std::make_shared<PreparedState>(*state);
    newState->playbackOrder = std::move(newOrder);
    newState->mutedLogicalPositions = std::move(newMuted);
    applyNewPreparedState(std::move(newState));
    apvts.getParameterAsValue(kSeedId).setValue(static_cast<int>(seed));
    return;
  }

  // No selection: shuffle within current window. When arrangement length != physical slice count
  // (after deletes or duplicates), use logical window so we only shuffle the visible positions.
  std::vector<size_t> logicalPositions;
  logicalPositions.reserve(numPositions);
  if (numPositions != totalSlices)
  {
    const auto [startLogical, endLogical] = getWindowLogicalPositionRange(*state);
    for (size_t logical = startLogical; logical <= endLogical && logical < numPositions; ++logical)
      logicalPositions.push_back(logical);
  }
  else
  {
    const auto [physStart, physEnd] = getWindowSliceRange(*state);
    if (physEnd <= physStart)
      return;
    for (size_t logical = 0; logical < numPositions; ++logical)
    {
      const size_t phys = state->playbackOrder[logical];
      if (phys >= physStart && phys < physEnd)
        logicalPositions.push_back(logical);
    }
  }

  if (logicalPositions.size() <= 1)
    return;

  const uint32_t seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt(1000000));
  const size_t count = logicalPositions.size();

  // Current physical indices at those logical positions (may contain duplicates).
  std::vector<size_t> physAtLogical;
  physAtLogical.reserve(count);
  for (size_t logical : logicalPositions)
    physAtLogical.push_back(state->playbackOrder[logical]);

  // Capture mute flags for the logical positions in the window so we can
  // permute them alongside the slices.
  std::vector<bool> mutedFlags;
  mutedFlags.reserve(count);
  for (size_t logical : logicalPositions)
    mutedFlags.push_back(state->mutedLogicalPositions.count(logical) != 0);

  // Permute indices into physAtLogical; keeps duplicates intact.
  const std::vector<size_t> order =
      sliceshuffle::SliceShuffleEngine::shuffledSliceOrder(count, seed, true);

  // Non-destructive: shuffle only within the set of logical positions in the window.
  std::vector<size_t> newOrder = state->playbackOrder;
  for (size_t i = 0; i < count; ++i)
  {
    const size_t dstLogical = logicalPositions[i];
    const size_t srcPhys = physAtLogical[order[i]];
    newOrder[dstLogical] = srcPhys;
  }

  // Build new muted set: start from current, then permute flags only within the window.
  std::unordered_set<size_t> newMuted = state->mutedLogicalPositions;
  for (size_t logical : logicalPositions)
    newMuted.erase(logical);
  for (size_t i = 0; i < count; ++i)
  {
    const bool wasMuted = mutedFlags[order[i]];
    if (wasMuted)
      newMuted.insert(logicalPositions[i]);
  }

  if (newOrder == state->playbackOrder && newMuted == state->mutedLogicalPositions)
    return;

  pushUndoEntry(state->playbackOrder, {}, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(newOrder);
  newState->mutedLogicalPositions = std::move(newMuted);

  applyNewPreparedState(std::move(newState));

  apvts.getParameterAsValue(kSeedId).setValue(static_cast<int>(seed));
}

void SliceShufflePluginProcessor::silenceSelectedSlices(const std::unordered_set<size_t>& selectedPositions)
{
  if (selectedPositions.empty())
    return;

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state)
    return;

  const size_t totalPositions = state->playbackOrder.size();
  if (totalPositions == 0)
    return;

  // Build new muted set by toggling the logical positions themselves (duplicates can be muted independently).
  std::unordered_set<size_t> newMuted = state->mutedLogicalPositions;
  bool anyChange = false;
  for (size_t pos : selectedPositions)
  {
    if (pos >= totalPositions)
      continue;
    if (newMuted.erase(pos) == 0)
    {
      newMuted.insert(pos);
    }
    anyChange = true;
  }
  if (!anyChange || newMuted == state->mutedLogicalPositions)
    return;

  pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->mutedLogicalPositions = std::move(newMuted);
  applyNewPreparedState(std::move(newState));
}

void SliceShufflePluginProcessor::reverseSelectedSlices(const std::unordered_set<size_t>& selectedPositions)
{
  if (selectedPositions.empty())
    return;

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state)
    return;

  const size_t totalPositions = state->playbackOrder.size();
  if (totalPositions == 0)
    return;

  std::unordered_set<size_t> newReversed = state->reversedLogicalPositions;
  bool anyChange = false;
  for (size_t pos : selectedPositions)
  {
    if (pos >= totalPositions)
      continue;
    if (newReversed.erase(pos) == 0)
      newReversed.insert(pos);
    anyChange = true;
  }
  if (!anyChange || newReversed == state->reversedLogicalPositions)
    return;

  pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->reversedLogicalPositions = std::move(newReversed);
  applyNewPreparedState(std::move(newState));
}

void SliceShufflePluginProcessor::duplicateSelectedSlices(const std::unordered_set<size_t>& selectedPositions)
{
  if (selectedPositions.empty())
    return;

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state)
    return;

  const size_t n = state->playbackOrder.size();
  if (n == 0 || state->slices.empty())
    return;

  std::vector<size_t> positions(selectedPositions.begin(), selectedPositions.end());
  std::sort(positions.begin(), positions.end());
  positions.erase(std::remove_if(positions.begin(), positions.end(), [n](size_t p) { return p >= n; }), positions.end());
  if (positions.empty())
    return;

  const size_t rightmost = positions.back();

  // Physical indices to duplicate (one copy of each selected slice, in order).
  std::vector<size_t> dupPhys;
  dupPhys.reserve(positions.size());
  for (size_t p : positions)
  {
    const size_t phys = state->playbackOrder[p];
    if (phys < state->slices.size())
      dupPhys.push_back(phys);
  }
  if (dupPhys.empty())
    return;

  const size_t dupCount = dupPhys.size();

  // Insert one copy of the selection immediately after the rightmost selected; shift everything
  // to the right by dupCount (arrangement grows).
  std::vector<size_t> newOrder;
  newOrder.reserve(n + dupCount);
  for (size_t i = 0; i <= rightmost; ++i)
    newOrder.push_back(state->playbackOrder[i]);
  for (size_t i = 0; i < dupCount; ++i)
    newOrder.push_back(dupPhys[i]);
  for (size_t i = rightmost + 1; i < n; ++i)
    newOrder.push_back(state->playbackOrder[i]);

  // Mute and reversed flags: keep for positions <= rightmost; shift by dupCount for positions > rightmost.
  // New slots (rightmost+1 .. rightmost+dupCount) are not muted or reversed.
  std::unordered_set<size_t> newMuted;
  for (size_t p : state->mutedLogicalPositions)
  {
    if (p <= rightmost)
      newMuted.insert(p);
    else
      newMuted.insert(p + dupCount);
  }
  std::unordered_set<size_t> newReversed;
  for (size_t p : state->reversedLogicalPositions)
  {
    if (p <= rightmost)
      newReversed.insert(p);
    else
      newReversed.insert(p + dupCount);
  }

  pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(newOrder);
  newState->mutedLogicalPositions = std::move(newMuted);
  newState->reversedLogicalPositions = std::move(newReversed);
  const bool wasFull = (n == state->slices.size());
  // Capture the logical range currently visible (so we can preserve it after we switch to extended mode).
  const auto [visibleStart, visibleEnd] = getWindowLogicalPositionRange(*state);
  applyNewPreparedState(std::move(newState));
  // When we go from full to extended arrangement, the slider starts driving the logical window.
  // Set it so the same logical range stays in view instead of jumping (e.g. to 0 or right by 1).
  if (wasFull && (n + dupCount) > state->slices.size())
  {
    const size_t nNew = n + dupCount;
    const int windowParam = juce::jlimit(
        4, 64,
        static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
    const size_t windowSlices = static_cast<size_t>(juce::jlimit(4, 64, (windowParam + 1) & ~1));
    const size_t countNew = juce::jmin(windowSlices, nNew);
    const size_t maxStartNew = (nNew > countNew) ? (nNew - countNew) : 0;
    const size_t startLogicalNew = std::min(visibleStart, maxStartNew);
    const float posNorm = (maxStartNew + 1 > 0)
        ? (static_cast<float>(startLogicalNew) / static_cast<float>(maxStartNew + 1))
        : 0.0f;
    if (auto* param = apvts.getParameter(kWindowPositionId))
      param->setValueNotifyingHost(posNorm);
  }
}

void SliceShufflePluginProcessor::removeSelectedSlices(const std::unordered_set<size_t>& selectedPositions)
{
  if (selectedPositions.empty())
    return;

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state)
    return;

  const size_t n = state->playbackOrder.size();
  if (n == 0)
    return;

  // Build new playback order: keep only logical positions not in selectedPositions (shift left).
  std::vector<size_t> newOrder;
  newOrder.reserve(n);
  std::unordered_set<size_t> newMuted;
  std::unordered_set<size_t> newReversed;
  for (size_t pos = 0; pos < n; ++pos)
  {
    if (selectedPositions.count(pos) != 0)
      continue;
    const size_t newPos = newOrder.size();
    newOrder.push_back(state->playbackOrder[pos]);
    if (state->mutedLogicalPositions.count(pos) != 0)
      newMuted.insert(newPos);
    if (state->reversedLogicalPositions.count(pos) != 0)
      newReversed.insert(newPos);
  }

  if (newOrder.size() == n)
    return; // nothing removed

  pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(newOrder);
  newState->mutedLogicalPositions = std::move(newMuted);
  newState->reversedLogicalPositions = std::move(newReversed);
  applyNewPreparedState(std::move(newState));

  // When arrangement length equals physical slice count, the bottom view uses the source window.
  // If that window no longer overlaps any remaining slice, adjust the window position so the
  // user can see slices again. When length != physical count (after duplicate/delete), we use
  // the logical window and the view already shows the correct remaining slices, so don't move
  // the slider (that would jump the overview and confuse the user).
  std::shared_ptr<const PreparedState> applied = getPreparedState();
  if (!applied || applied->playbackOrder.empty() || applied->slices.empty())
    return;
  if (applied->playbackOrder.size() != applied->slices.size())
    return;

  const size_t numSlices = applied->slices.size();
  const int windowParam = juce::jlimit(
      4, 64,
      static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
  const size_t windowSlices = static_cast<size_t>(juce::jlimit(4, 64, (windowParam + 1) & ~1));
  if (windowSlices >= numSlices)
    return;
  const juce::int64 totalSamples = applied->lengthInSamples;
  auto [windowStart, windowEnd] = getWindowRangeSnappedToSlices(*applied);
  windowStart = juce::jlimit(juce::int64(0), totalSamples, windowStart);
  windowEnd = juce::jlimit(juce::int64(0), totalSamples, windowEnd);
  bool anyInWindow = false;
  for (size_t logical = 0; logical < applied->playbackOrder.size(); ++logical)
  {
    const size_t phys = applied->playbackOrder[logical];
    if (phys >= numSlices)
      continue;
    const auto& sl = applied->slices[phys];
    const juce::int64 slStart = static_cast<juce::int64>(sl.startSample);
    const juce::int64 slEnd = slStart + static_cast<juce::int64>(sl.lengthSamples);
    if (slEnd > windowStart && slStart < windowEnd)
    {
      anyInWindow = true;
      break;
    }
  }
  if (!anyInWindow)
  {
    const size_t firstPhys = applied->playbackOrder[0];
    if (firstPhys < numSlices)
    {
      const size_t maxStart = numSlices - windowSlices;
      const size_t startIdx = std::min(firstPhys, maxStart);
      if (auto* param = apvts.getParameter(kWindowPositionId))
      {
        const float posNorm = (maxStart > 0) ? (static_cast<float>(startIdx) / static_cast<float>(maxStart)) : 0.0f;
        param->setValueNotifyingHost(posNorm);
      }
    }
  }
}

std::vector<size_t> SliceShufflePluginProcessor::moveSelectedSlicesInOrder(
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
  const std::vector<sliceshuffle::Slice>& slices = state->slices;
  std::unordered_set<size_t> newMuted = state->mutedLogicalPositions;

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

      // Rotate mute flags for this segment in the same way (chips move right).
      std::vector<bool> oldMuteSeg(segLen);
      for (size_t i = 0; i < segLen; ++i)
        oldMuteSeg[i] = newMuted.count(runStart + i) != 0;
      for (size_t i = 0; i < segLen; ++i)
      {
        const size_t idx = runStart + i;
        newMuted.erase(idx);
      }
      for (size_t i = 0; i < segLen; ++i)
      {
        const size_t dstIdx = runStart + ((i + 1) % segLen);
        if (oldMuteSeg[i])
          newMuted.insert(dstIdx);
      }

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

      // Rotate mute flags for this segment in the same way (chips move left).
      std::vector<bool> oldMuteSeg(segLen);
      for (size_t i = 0; i < segLen; ++i)
        oldMuteSeg[i] = newMuted.count(segStart + i) != 0;
      for (size_t i = 0; i < segLen; ++i)
      {
        const size_t idx = segStart + i;
        newMuted.erase(idx);
      }
      for (size_t i = 0; i < segLen; ++i)
      {
        const size_t dstIdx = segStart + ((i + segLen - 1) % segLen);
        if (oldMuteSeg[i])
          newMuted.insert(dstIdx);
      }

    }
  }

  if (order == state->playbackOrder && newMuted == state->mutedLogicalPositions)
    return {};
  pushUndoEntry(state->playbackOrder, selectedPositions, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = order;
  newState->mutedLogicalPositions = std::move(newMuted);

  applyNewPreparedState(std::move(newState));
  return order;
}

std::pair<juce::int64, juce::int64> SliceShufflePluginProcessor::getWindowRangeSnappedToSlices() const
{
  auto state = getPreparedState();
  if (!state)
    return {0, 0};
  return getWindowRangeSnappedToSlices(*state);
}

std::pair<juce::int64, juce::int64> SliceShufflePluginProcessor::getWindowRangeSnappedToSlices(
    const PreparedState& state) const
{
  const juce::int64 totalSamples = state.lengthInSamples;
  if (totalSamples <= 0)
    return {0, 0};

  const std::vector<sliceshuffle::Slice>& slices = state.slices;
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

  const sliceshuffle::Slice& first = slices[startIdx];
  const sliceshuffle::Slice& last = slices[startIdx + windowSlices - 1];
  const juce::int64 startSnap = static_cast<juce::int64>(first.startSample);
  const juce::int64 endSnap = static_cast<juce::int64>(last.startSample) + static_cast<juce::int64>(last.lengthSamples);

  return {startSnap, juce::jmin(endSnap, totalSamples)};
}

void SliceShufflePluginProcessor::startPreview()
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

void SliceShufflePluginProcessor::stopPreview()
{
  previewActive_.store(false);
}

bool SliceShufflePluginProcessor::isPreviewActive() const
{
  return previewActive_.load();
}

void SliceShufflePluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
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
  const auto& mutedPositions = state->mutedLogicalPositions;

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

      const size_t totalPositions = state->playbackOrder.size();
      const size_t orderIndex = static_cast<size_t>(sliceIndex);
      if (orderIndex >= totalPositions)
        continue;

      // Non-destructive mute: skip starting voices for muted logical positions.
      if (!mutedPositions.empty() && mutedPositions.count(orderIndex) != 0)
        continue;

      const size_t srcSliceIdx = state->playbackOrder[orderIndex];
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
      v.reversed = !state->reversedLogicalPositions.empty() &&
                  state->reversedLogicalPositions.count(orderIndex) != 0;
    }
  }

  midiMessages.clear();

  // Render voices
  const int blockSamples = buffer.getNumSamples();
  for (auto& v : voices_)
  {
    if (!v.active || v.sliceIndex < 0 || v.sliceIndex >= static_cast<int>(state->slices.size()))
      continue;

    const sliceshuffle::Slice& sl = state->slices[static_cast<size_t>(v.sliceIndex)];
    const size_t sliceLen = sl.lengthSamples;
    const size_t start = sl.startSample;
    const int fadeSamples = fadeSamplesForSlice(state->sampleRate, sliceLen);

    for (int i = 0; i < blockSamples; ++i)
    {
      if (v.samplePosition >= sliceLen)
      {
        v.active = false;
        break;
      }

      float gain = 1.0f;
      if (v.samplePosition < static_cast<size_t>(fadeSamples))
        gain = (static_cast<float>(v.samplePosition) + 1.0f) / static_cast<float>(fadeSamples);
      else if (v.samplePosition >= sliceLen - static_cast<size_t>(fadeSamples) && sliceLen > static_cast<size_t>(fadeSamples))
        gain = static_cast<float>(sliceLen - v.samplePosition) / static_cast<float>(fadeSamples);

      const size_t srcSample = v.reversed ? (start + sliceLen - 1 - v.samplePosition) : (start + v.samplePosition);
      for (int ch = 0; ch < numOutCh; ++ch)
      {
        const int srcCh = std::min(ch, numSrcCh - 1);
        const float sample = v.gain * gain * state->buffer.getSample(srcCh, static_cast<int>(srcSample));
        buffer.addSample(ch, i, sample);
      }
      ++v.samplePosition;
    }
  }
}

bool SliceShufflePluginProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SliceShufflePluginProcessor::createEditor()
{
  return new SliceShufflePluginEditor(*this);
}

void SliceShufflePluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
  auto xml = createFullStateXml();
  if (xml)
    copyXmlToBinary(*xml, destData);
}

void SliceShufflePluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml)
  {
    // Project restore: restore params, sample path, slice order, and editor size.
    // Reload WAV from path (or set Missing), apply saved playback order/muted/reversed.
    restoreStateFromXml(*xml, true);
  }
}

std::unique_ptr<juce::XmlElement> SliceShufflePluginProcessor::createFullStateXml() const
{
  auto vt = const_cast<juce::AudioProcessorValueTreeState&>(apvts).copyState();
  std::unique_ptr<juce::XmlElement> xml(vt.createXml());
  if (!xml)
    return nullptr;

  // Version for future-proofing
  xml->setAttribute(kStateVersionAttr, kCurrentStateVersion);

  // Persist sample path and metadata (for missing-file detection / relink)
  {
    std::shared_lock lock(stateMutex_);
    xml->setAttribute(kSamplePathAttr, loadedSamplePath_);
    if (loadedSamplePath_.isNotEmpty())
    {
      xml->setAttribute(kFileNameAttr, loadedSampleDisplayName_);
      juce::File f(loadedSamplePath_);
      if (f.existsAsFile())
      {
        xml->setAttribute(kFileSizeAttr, juce::String(static_cast<juce::int64>(f.getSize())));
        xml->setAttribute(kFileLastModifiedAttr, juce::String(f.getLastModificationTime().toMilliseconds()));
      }
    }
  }

  // Persist editor size so host project restore reopens same window size
  if (savedEditorW_ > 0 && savedEditorH_ > 0)
  {
    xml->setAttribute(kEditorWidthAttr, savedEditorW_);
    xml->setAttribute(kEditorHeightAttr, savedEditorH_);
  }

  // Persist playback order (and duplicated slices) and muted/reversed positions.
  // Always set these from current prepared state so we overwrite any stale values
  // that may be in the APVTS tree from a previous restore (replaceState stores
  // our custom attributes in the tree; copyState/createXml would otherwise re-serialize them).
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
    else
      xml->setAttribute(kPlaybackOrderAttr, "");

    juce::String mutedString;
    bool first = true;
    for (size_t pos : prepared->mutedLogicalPositions)
    {
      if (!first)
        mutedString << ",";
      mutedString << static_cast<int>(pos);
      first = false;
    }
    xml->setAttribute(kMutedPositionsAttr, mutedString);

    juce::String revString;
    first = true;
    for (size_t pos : prepared->reversedLogicalPositions)
    {
      if (!first)
        revString << ",";
      revString << static_cast<int>(pos);
      first = false;
    }
    xml->setAttribute(kReversedPositionsAttr, revString);
  }

  return xml;
}

void SliceShufflePluginProcessor::restoreStateFromXml(const juce::XmlElement& xml, bool restoreSamplePath)
{
  // Restore APVTS (all parameters)
  apvts.replaceState(juce::ValueTree::fromXml(xml));

  // Versioning hook (currently unused but kept for future migrations)
  const int version = xml.getIntAttribute(kStateVersionAttr, 0);
  juce::ignoreUnused(version);

  // Restore any saved playback order and muted positions for application after sample load
  pendingPlaybackOrder_.clear();
  pendingMutedLogicalPositions_.clear();
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
  const juce::String mutedStr = xml.getStringAttribute(kMutedPositionsAttr, {});
  if (mutedStr.isNotEmpty())
  {
    juce::StringArray tokens;
    tokens.addTokens(mutedStr, ",", "");
    tokens.removeEmptyStrings();
    for (const auto& t : tokens)
    {
      const int v = t.getIntValue();
      if (v >= 0)
        pendingMutedLogicalPositions_.insert(static_cast<size_t>(v));
    }
  }
  pendingReversedLogicalPositions_.clear();
  const juce::String revStr = xml.getStringAttribute(kReversedPositionsAttr, {});
  if (revStr.isNotEmpty())
  {
    juce::StringArray tokens;
    tokens.addTokens(revStr, ",", "");
    tokens.removeEmptyStrings();
    for (const auto& t : tokens)
    {
      const int v = t.getIntValue();
      if (v >= 0)
        pendingReversedLogicalPositions_.insert(static_cast<size_t>(v));
    }
  }

  // Restore saved editor size so editor opens at same size when created
  savedEditorW_ = xml.getIntAttribute(kEditorWidthAttr, 0);
  savedEditorH_ = xml.getIntAttribute(kEditorHeightAttr, 0);

  if (!restoreSamplePath)
  {
    // Session restore: start with no sample loaded, default window size and granularity.
    clearSample();
    if (auto* param = apvts.getParameter(kWindowBeatsId))
    {
      // Default window = 4 slices; normalized = (4 - 4) / (64 - 4)
      param->setValueNotifyingHost(0.0f);
    }
    if (auto* param = apvts.getParameter(kGranularityId))
    {
      // Default granularity = 1 beat (index 2); range is 0..4
      param->setValueNotifyingHost(2.0f / 4.0f); // normalized: index 2 of 5 = 2/4
    }
    return;
  }

  // Preset/project restore: restore sample path and kick off async load (or mark Missing)
  const juce::String path = xml.getStringAttribute(kSamplePathAttr, {});
  if (path.isEmpty())
    return;

  juce::File f(path);
  if (f.existsAsFile())
  {
    loadSampleFromFile(f, true);  // apply saved playback order and muted positions when load completes
  }
  else
  {
    std::unique_lock lock(stateMutex_);
    loadedSamplePath_ = path;
    loadedSampleDisplayName_ = xml.getStringAttribute(kFileNameAttr, f.getFileName());
    setLoadStatus(LoadStatus::Missing);
  }
}

void SliceShufflePluginProcessor::setSavedEditorSize(int w, int h)
{
  if (w > 0 && h > 0)
  {
    savedEditorW_ = w;
    savedEditorH_ = h;
  }
}

bool SliceShufflePluginProcessor::savePresetToFile(const juce::File& file) const
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

bool SliceShufflePluginProcessor::loadPresetFromFile(const juce::File& file)
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

void SliceShufflePluginProcessor::resetArrangement()
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

  if (state->playbackOrder == identityOrder && state->mutedLogicalPositions.empty() && state->reversedLogicalPositions.empty())
    return;
  pushUndoEntry(state->playbackOrder, {}, state->mutedLogicalPositions, state->reversedLogicalPositions);
  auto newState = std::make_shared<PreparedState>(*state);
  newState->playbackOrder = std::move(identityOrder);
  newState->mutedLogicalPositions.clear();
  newState->reversedLogicalPositions.clear();

  applyNewPreparedState(std::move(newState));
}

std::pair<size_t, size_t> SliceShufflePluginProcessor::getWindowSliceRange(const PreparedState& state) const
{
  auto [startSample, endSample] = getWindowRangeSnappedToSlices(state);
  if (endSample <= startSample)
    return {0, 0};

  const std::vector<sliceshuffle::Slice>& slices = state.slices;
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

std::pair<size_t, size_t> SliceShufflePluginProcessor::getWindowLogicalPositionRange(
    const PreparedState& state) const
{
  const size_t n = state.playbackOrder.size();
  if (state.slices.empty() || n == 0)
    return {0, 0};

  const int windowParam = juce::jlimit(
      4, 64,
      static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
  const size_t windowSlices = static_cast<size_t>(juce::jlimit(4, 64, (windowParam + 1) & ~1));

  // When arrangement length differs from physical slice count (after deletes or duplicates),
  // show exactly windowSlices logical positions so the visible slice count matches the slider.
  if (n != state.slices.size())
  {
    const size_t count = juce::jmin(windowSlices, n);
    const size_t maxStart = (n > count) ? (n - count) : 0;
    const float posNorm = apvts.getRawParameterValue(kWindowPositionId)->load();
    const size_t startLogical = std::min(
        maxStart,
        static_cast<size_t>(std::round(static_cast<double>(posNorm) * static_cast<double>(maxStart + 1))));
    const size_t endLogical = startLogical + (count > 0 ? count - 1 : 0);
    return {startLogical, endLogical};
  }

  // Full arrangement: window is in source (physical) space; find logical positions that overlap.
  auto [windowStart, windowEnd] = getWindowRangeSnappedToSlices(state);
  if (windowEnd <= windowStart)
    return {0, n > 0 ? n - 1 : 0};

  juce::int64 cumStart = 0;
  size_t minLogical = n;
  size_t maxLogical = 0;
  for (size_t logical = 0; logical < n; ++logical)
  {
    const size_t phys = state.playbackOrder[logical];
    if (phys >= state.slices.size())
      continue;
    const juce::int64 len = static_cast<juce::int64>(state.slices[phys].lengthSamples);
    const juce::int64 cumEnd = cumStart + len;
    if (cumEnd > windowStart && cumStart < windowEnd)
    {
      if (minLogical > logical)
        minLogical = logical;
      maxLogical = logical;
    }
    cumStart = cumEnd;
  }
  if (minLogical > maxLogical)
    return {0, n > 0 ? n - 1 : 0};
  return {minLogical, maxLogical};
}

void SliceShufflePluginProcessor::renderWindowToBuffer(const PreparedState& state,
                                                 juce::AudioBuffer<float>& out) const
{
  const size_t totalSlices = state.slices.size();
  const size_t numPositions = state.playbackOrder.size();
  if (numPositions == 0)
  {
    out.setSize(0, 0);
    return;
  }

  const auto [logicalStart, logicalEnd] = getWindowLogicalPositionRange(state);
  if (logicalEnd < logicalStart || logicalStart >= numPositions)
  {
    out.setSize(0, 0);
    return;
  }

  const int numCh = state.buffer.getNumChannels();
  size_t totalSamples = 0;

  // First pass: logical positions that belong to the current window.
  std::vector<size_t> logicalPositions;
  logicalPositions.reserve(numPositions);
  for (size_t logical = logicalStart; logical <= logicalEnd && logical < numPositions; ++logical)
  {
    const size_t srcIdx = state.playbackOrder[logical];
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
  for (size_t i = 0; i < logicalPositions.size(); ++i)
  {
    const size_t logical = logicalPositions[i];
    const size_t srcIdx = state.playbackOrder[logical];
    if (srcIdx >= totalSlices)
      continue;
    const auto& sl = state.slices[srcIdx];
    const int len = static_cast<int>(sl.lengthSamples);
    const int srcStart = static_cast<int>(sl.startSample);
    const int fadeSamples = fadeSamplesForSlice(state.sampleRate, sl.lengthSamples);

    // Only apply fades at boundaries that are discontinuous in the source.
    // When playback order is original (0,1,2,...), adjacent slices are continuous — skip fades to avoid a dip.
    const size_t prevSrcIdx = (i > 0) ? state.playbackOrder[logicalPositions[i - 1]] : totalSlices;
    const size_t nextSrcIdx = (i + 1 < logicalPositions.size()) ? state.playbackOrder[logicalPositions[i + 1]] : totalSlices;
    const bool continuousWithPrev = (i > 0 && prevSrcIdx + 1 == srcIdx);
    const bool continuousWithNext = (i + 1 < logicalPositions.size() && nextSrcIdx == srcIdx + 1);
    const bool applyFadeIn = !continuousWithPrev;
    const bool applyFadeOut = !continuousWithNext;

    const bool isMuted = !state.mutedLogicalPositions.empty() &&
                         state.mutedLogicalPositions.count(logical) != 0;
    const bool isReversed = !state.reversedLogicalPositions.empty() &&
                            state.reversedLogicalPositions.count(logical) != 0;

    if (!isMuted)
    {
      if (isReversed)
      {
        for (int ch = 0; ch < numCh; ++ch)
          for (int j = 0; j < len; ++j)
            out.setSample(ch, static_cast<int>(writePos) + j,
                         state.buffer.getSample(ch, srcStart + len - 1 - j));
      }
      else
      {
        for (int ch = 0; ch < numCh; ++ch)
          out.copyFrom(ch, static_cast<int>(writePos), state.buffer, ch, srcStart, len);
      }
      applySliceFades(out, static_cast<int>(writePos), len, fadeSamples, fadeSamples, applyFadeIn, applyFadeOut);
    }
    writePos += static_cast<size_t>(len);
  }
}

void SliceShufflePluginProcessor::renderSampleRangeToBuffer(const PreparedState& state,
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
  const size_t numPositions = state.playbackOrder.size();
  if (numPositions == 0)
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
  for (size_t logical = 0; logical < numPositions; ++logical)
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
  for (size_t logical = 0; logical < numPositions; ++logical)
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
    const size_t offsetInSliceStart = static_cast<size_t>(overlapStart - sliceStart);
    const size_t sliceLen = sl.lengthSamples;
    const int fadeSamples = fadeSamplesForSlice(state.sampleRate, sliceLen);
    // Only apply fades at boundaries that are discontinuous in the source (same as renderWindowToBuffer).
    const size_t prevSrcIdx = (logical > 0) ? state.playbackOrder[logical - 1] : totalSlices;
    const size_t nextSrcIdx = (logical + 1 < numPositions) ? state.playbackOrder[logical + 1] : totalSlices;
    const bool continuousWithPrev = (logical > 0 && prevSrcIdx + 1 == srcIdx);
    const bool continuousWithNext = (logical + 1 < numPositions && nextSrcIdx == srcIdx + 1);
    const bool atSliceStart = (offsetInSliceStart == 0);
    const bool atSliceEnd = (offsetInSliceStart + static_cast<size_t>(len) == sliceLen);
    const bool applyFadeInAtStart = atSliceStart && !continuousWithPrev;
    const bool applyFadeOutAtEnd = atSliceEnd && !continuousWithNext;

    const bool isMuted = !state.mutedLogicalPositions.empty() &&
                         state.mutedLogicalPositions.count(logical) != 0;
    const bool isReversed = !state.reversedLogicalPositions.empty() &&
                            state.reversedLogicalPositions.count(logical) != 0;
    if (!isMuted)
    {
      if (isReversed)
      {
        for (int ch = 0; ch < numCh; ++ch)
          for (int j = 0; j < len; ++j)
            out.setSample(ch, static_cast<int>(writePos) + j,
                         state.buffer.getSample(ch, srcStart + len - 1 - j));
      }
      else
      {
        for (int ch = 0; ch < numCh; ++ch)
          out.copyFrom(ch, static_cast<int>(writePos), state.buffer, ch, srcStart, len);
      }
      applySliceFadesSegment(out, static_cast<int>(writePos), len, offsetInSliceStart, sliceLen, fadeSamples,
                            applyFadeInAtStart, applyFadeOutAtEnd);
    }
    writePos += static_cast<size_t>(len);
  }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
  return static_cast<juce::AudioProcessor*>(new SliceShufflePluginProcessor());
}
