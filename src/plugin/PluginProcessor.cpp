#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../cli/WavLoader.h"
#include "../dsp/SlicerEngine.h"
#include <cmath>
#include <numeric>

namespace {
constexpr const char* kBpmId = "bpm";
constexpr const char* kGranularityId = "granularity";
constexpr const char* kSeedId = "seed";
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";

// Beats per slice for each granularity index 0..5
constexpr double kBeatsPerSlice[] = {0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
constexpr int kNumGranularityChoices = 6;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
  juce::AudioProcessorValueTreeState::ParameterLayout layout;
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kBpmId, 1}, "BPM",
      juce::NormalisableRange<float>(40.f, 240.f, 0.1f, 1.f), 120.f));
  layout.add(std::make_unique<juce::AudioParameterChoice>(
      juce::ParameterID{kGranularityId, 1}, "Granularity",
      juce::StringArray{"1/2", "1", "2", "4", "8", "16"}, 1)); // 1 = 1 beat
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kSeedId, 1}, "Seed", 0, 999999, 0));
  layout.add(std::make_unique<juce::AudioParameterInt>(
      juce::ParameterID{kWindowBeatsId, 1}, "Window", 2, 64, 16));
  layout.add(std::make_unique<juce::AudioParameterFloat>(
      juce::ParameterID{kWindowPositionId, 1}, "Window position",
      juce::NormalisableRange<float>(0.f, 1.f, 0.01f, 1.f), 0.f));
  return layout;
}
} // namespace

SlicerPluginProcessor::SlicerPluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(
        BusesProperties()
            .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "SlicerParams", createParameterLayout())
#endif
{
#ifdef JucePlugin_PreferredChannelConfigurations
  apvts = juce::AudioProcessorValueTreeState(*this, nullptr, "SlicerParams", createParameterLayout());
#endif
  voices_.resize(kMaxVoices);
}

SlicerPluginProcessor::~SlicerPluginProcessor() = default;

const juce::String SlicerPluginProcessor::getName() const { return JucePlugin_Name; }

bool SlicerPluginProcessor::acceptsMidi() const { return true; }
bool SlicerPluginProcessor::producesMidi() const { return false; }
bool SlicerPluginProcessor::isMidiEffect() const { return false; }
double SlicerPluginProcessor::getTailLengthSeconds() const { return 0.0; }

int SlicerPluginProcessor::getNumPrograms() { return 1; }
int SlicerPluginProcessor::getCurrentProgram() { return 0; }
void SlicerPluginProcessor::setCurrentProgram(int) {}
const juce::String SlicerPluginProcessor::getProgramName(int) { return {}; }
void SlicerPluginProcessor::changeProgramName(int, const juce::String&) {}

void SlicerPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  hostSampleRate_ = sampleRate;
  hostBlockSize_ = samplesPerBlock;
}

void SlicerPluginProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SlicerPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
      || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

std::shared_ptr<const PreparedState> SlicerPluginProcessor::getPreparedState() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_;
}

SlicerPluginProcessor::LoadStatus SlicerPluginProcessor::getLoadStatus() const
{
  return loadStatus_.load();
}

juce::String SlicerPluginProcessor::getLoadedSampleDisplayName() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSampleDisplayName_;
}

juce::String SlicerPluginProcessor::getLoadedSamplePath() const
{
  std::shared_lock lock(stateMutex_);
  return loadedSamplePath_;
}

int SlicerPluginProcessor::getNumSlices() const
{
  std::lock_guard lock(preparedStateMutex_);
  return preparedState_ ? static_cast<int>(preparedState_->slices.size()) : 0;
}

juce::String SlicerPluginProcessor::getLoadErrorText() const
{
  std::shared_lock lock(stateMutex_);
  return loadErrorText_;
}

void SlicerPluginProcessor::setLoadStatus(LoadStatus s)
{
  loadStatus_.store(s);
}

void SlicerPluginProcessor::setLoadError(const juce::String& text)
{
  std::unique_lock lock(stateMutex_);
  loadErrorText_ = text;
}

void SlicerPluginProcessor::applyNewPreparedState(std::shared_ptr<const PreparedState> state)
{
  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_ = std::move(state);
  }
  setLoadStatus(state ? LoadStatus::Ready : LoadStatus::Idle);
  if (!state)
    setLoadError({});
}

void SlicerPluginProcessor::buildPreparedStateFromBuffer(juce::AudioBuffer<float> buffer,
                                                         double sampleRate,
                                                         const juce::String& displayName,
                                                         const juce::String& path)
{
  const auto totalSamples = static_cast<size_t>(buffer.getNumSamples());
  if (totalSamples == 0 || sampleRate <= 0)
    return;

  slicer::SlicerEngine engine;
  engine.setBpm(static_cast<double>(*apvts.getRawParameterValue(kBpmId)));
  const int granIndex = juce::jlimit(
      0, kNumGranularityChoices - 1,
      static_cast<int>(std::round(apvts.getRawParameterValue(kGranularityId)->load())));
  const double beatsPerSlice = kBeatsPerSlice[granIndex];
  auto slices = engine.computeSlices(totalSamples, sampleRate, beatsPerSlice);
  if (slices.empty())
    return;

  const uint32_t seed = static_cast<uint32_t>(*apvts.getRawParameterValue(kSeedId));
  auto order = slicer::SlicerEngine::shuffledSliceOrder(slices.size(), seed, true);

  auto state = std::make_shared<PreparedState>();
  state->buffer = std::move(buffer);
  state->sampleRate = sampleRate;
  state->lengthInSamples = static_cast<juce::int64>(totalSamples);
  state->slices = std::move(slices);
  state->playbackOrder = std::move(order);

  {
    std::unique_lock lock(stateMutex_);
    loadedSampleDisplayName_ = displayName;
    loadedSamplePath_ = path;
    loadErrorText_.clear();
  }

  applyNewPreparedState(std::move(state));
}

void SlicerPluginProcessor::loadSampleFromFile(const juce::File& file)
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
      buildPreparedStateFromBuffer(std::move(buf), sr, displayName, path);
    });
  });
}

void SlicerPluginProcessor::clearSample()
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

void SlicerPluginProcessor::regenerateSliceMap()
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
  buildPreparedStateFromBuffer(
      juce::AudioBuffer<float>(state->buffer), state->sampleRate, displayName, path);
}

void SlicerPluginProcessor::rearrangeSample()
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  if (!state || state->buffer.getNumSamples() == 0)
    return;

  const auto totalSamples = static_cast<size_t>(state->buffer.getNumSamples());
  const double sampleRate = state->sampleRate;
  const double bpm = static_cast<double>(*apvts.getRawParameterValue(kBpmId));
  if (sampleRate <= 0 || bpm <= 0)
    return;

  const double secondsPerBeat = 60.0 / bpm;
  const double totalBeats = static_cast<double>(totalSamples) / (sampleRate * secondsPerBeat);
  const int windowBeatsParam =
      juce::jlimit(2, 64, static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
  const double windowBeats = static_cast<double>(windowBeatsParam);
  const float posNorm = apvts.getRawParameterValue(kWindowPositionId)->load();
  const double startBeat =
      totalBeats <= windowBeats ? 0.0 : posNorm * std::max(0.0, totalBeats - windowBeats);
  const size_t startSample =
      static_cast<size_t>(std::round(startBeat * secondsPerBeat * sampleRate));
  const size_t endSample = std::min(
      totalSamples,
      static_cast<size_t>(std::round((startBeat + windowBeats) * secondsPerBeat * sampleRate)));
  const size_t windowLengthSamples = (endSample > startSample) ? (endSample - startSample) : 0;
  if (windowLengthSamples == 0)
    return;

  slicer::SlicerEngine engine;
  engine.setBpm(bpm);
  const int granIndex = juce::jlimit(
      0, kNumGranularityChoices - 1,
      static_cast<int>(std::round(apvts.getRawParameterValue(kGranularityId)->load())));
  const double beatsPerSlice = kBeatsPerSlice[granIndex];
  std::vector<slicer::Slice> windowSlices =
      engine.computeSlices(windowLengthSamples, sampleRate, beatsPerSlice);
  if (windowSlices.empty())
    return;

  const size_t numSlices = windowSlices.size();
  const uint32_t seed = static_cast<uint32_t>(juce::Random::getSystemRandom().nextInt(1000000));
  const std::vector<size_t> order =
      slicer::SlicerEngine::shuffledSliceOrder(numSlices, seed, true);

  const int numCh = state->buffer.getNumChannels();
  juce::AudioBuffer<float> newBuffer(state->buffer.getNumChannels(), state->buffer.getNumSamples());
  newBuffer.makeCopyOf(state->buffer);

  size_t writePos = 0;
  for (size_t i = 0; i < numSlices; ++i)
  {
    const size_t srcIdx = order[i];
    const slicer::Slice& srcSl = windowSlices[srcIdx];
    const size_t srcStartInFull = startSample + srcSl.startSample;
    const size_t dstStartInFull = startSample + writePos;
    for (int ch = 0; ch < numCh; ++ch)
    {
      newBuffer.copyFrom(ch, static_cast<int>(dstStartInFull), state->buffer,
                        ch, static_cast<int>(srcStartInFull),
                        static_cast<int>(srcSl.lengthSamples));
    }
    writePos += srcSl.lengthSamples;
  }

  engine.setBpm(bpm);
  std::vector<slicer::Slice> fullSlices =
      engine.computeSlices(totalSamples, sampleRate, beatsPerSlice);
  std::vector<size_t> identityOrder(fullSlices.size());
  std::iota(identityOrder.begin(), identityOrder.end(), size_t(0));

  auto newState = std::make_shared<PreparedState>();
  newState->buffer = std::move(newBuffer);
  newState->sampleRate = state->sampleRate;
  newState->lengthInSamples = static_cast<juce::int64>(totalSamples);
  newState->slices = std::move(fullSlices);
  newState->playbackOrder = std::move(identityOrder);

  {
    std::lock_guard lock(preparedStateMutex_);
    preparedState_ = std::move(newState);
  }

  apvts.getParameterAsValue(kSeedId).setValue(static_cast<int>(seed));
}

void SlicerPluginProcessor::startPreview()
{
  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }
  previewSamplePos_.store(0);
  if (!state || state->buffer.getNumSamples() == 0 || state->sampleRate <= 0)
  {
    previewStartSample_.store(0);
    previewLengthSamples_.store(0);
    previewActive_.store(true);
    return;
  }
  const juce::int64 totalSamples = state->lengthInSamples;
  const double sampleRate = state->sampleRate;
  const double bpm = static_cast<double>(apvts.getRawParameterValue(kBpmId)->load());
  if (bpm <= 0)
  {
    previewStartSample_.store(0);
    previewLengthSamples_.store(std::min(totalSamples, static_cast<juce::int64>(kPreviewLengthSeconds * hostSampleRate_)));
    previewActive_.store(true);
    return;
  }
  const double secondsPerBeat = 60.0 / bpm;
  const double totalBeats = static_cast<double>(totalSamples) / (sampleRate * secondsPerBeat);
  const int windowBeatsParam = juce::jlimit(
      2, 64,
      static_cast<int>(std::round(apvts.getRawParameterValue(kWindowBeatsId)->load())));
  const double windowBeats = static_cast<double>(windowBeatsParam);
  const float posNorm = apvts.getRawParameterValue(kWindowPositionId)->load();
  const double startBeat = totalBeats <= windowBeats ? 0.0 : static_cast<double>(posNorm) * std::max(0.0, totalBeats - windowBeats);
  const juce::int64 startSample = static_cast<juce::int64>(std::round(startBeat * secondsPerBeat * sampleRate));
  const juce::int64 endSample = std::min(
      totalSamples,
      static_cast<juce::int64>(std::round((startBeat + windowBeats) * secondsPerBeat * sampleRate)));
  const juce::int64 windowLengthSamples = std::max(juce::int64(0), endSample - startSample);
  const double windowDurationSeconds = (windowLengthSamples > 0 && sampleRate > 0)
      ? (static_cast<double>(windowLengthSamples) / sampleRate) : 0.0;

  if (windowDurationSeconds > 0.0 && windowDurationSeconds < kPreviewLengthSeconds)
  {
    previewStartSample_.store(startSample);
    previewLengthSamples_.store(windowLengthSamples);
  }
  else
  {
    previewStartSample_.store(0);
    previewLengthSamples_.store(std::min(totalSamples, static_cast<juce::int64>(kPreviewLengthSeconds * hostSampleRate_)));
  }
  previewActive_.store(true);
}

void SlicerPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
  juce::ScopedNoDenormals noDenormals;
  buffer.clear();

  std::shared_ptr<const PreparedState> state;
  {
    std::lock_guard lock(preparedStateMutex_);
    state = preparedState_;
  }

  // Preview: play region (window if < 5 sec, else first 5 sec)
  if (previewActive_.load() && state && state->buffer.getNumSamples() > 0)
  {
    const int numSrcCh = state->buffer.getNumChannels();
    const juce::int64 totalSamples = state->lengthInSamples;
    const juce::int64 startS = previewStartSample_.load();
    const juce::int64 lenS = previewLengthSamples_.load();
    const int blockSamples = buffer.getNumSamples();
    const int numOutCh = buffer.getNumChannels();
    juce::int64 pos = previewSamplePos_.load();

    for (int i = 0; i < blockSamples; ++i)
    {
      if (lenS <= 0 || pos >= lenS)
      {
        previewActive_.store(false);
        break;
      }
      const juce::int64 srcPos = startS + pos;
      if (srcPos < 0 || srcPos >= totalSamples)
      {
        previewActive_.store(false);
        break;
      }
      for (int ch = 0; ch < numOutCh; ++ch)
      {
        const int srcCh = std::min(ch, numSrcCh - 1);
        buffer.addSample(ch, i, state->buffer.getSample(srcCh, static_cast<int>(srcPos)));
      }
      ++pos;
    }
    previewSamplePos_.store(pos);
    if (lenS > 0 && pos >= lenS)
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

    const slicer::Slice& sl = state->slices[static_cast<size_t>(v.sliceIndex)];
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

bool SlicerPluginProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SlicerPluginProcessor::createEditor()
{
  return new SlicerPluginEditor(*this);
}

void SlicerPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
  auto state = apvts.copyState();
  std::unique_ptr<juce::XmlElement> xml(state.createXml());
  if (xml.get())
  {
    xml->setAttribute("samplePath", loadedSamplePath_);
    copyXmlToBinary(*xml, destData);
  }
}

void SlicerPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
  if (xml.get())
  {
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    juce::String path = xml->getStringAttribute("samplePath", {});
    if (path.isNotEmpty())
    {
      juce::File f(path);
      if (f.existsAsFile())
        loadSampleFromFile(f);
      else
      {
        std::unique_lock lock(stateMutex_);
        loadedSamplePath_ = path;
        loadedSampleDisplayName_ = f.getFileName();
        setLoadStatus(LoadStatus::Missing);
      }
    }
  }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
  return static_cast<juce::AudioProcessor*>(new SlicerPluginProcessor());
}
