#pragma once

#include <JuceHeader.h>
#include "PreparedState.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

class SlicerPluginProcessor : public juce::AudioProcessor
{
public:
  enum class LoadStatus { Idle, Loading, Ready, Missing, Error };

  SlicerPluginProcessor();
  ~SlicerPluginProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
  bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

  void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

  juce::AudioProcessorEditor* createEditor() override;
  bool hasEditor() const override;

  const juce::String getName() const override;
  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String& newName) override;

  void getStateInformation(juce::MemoryBlock& destData) override;
  void setStateInformation(const void* data, int sizeInBytes) override;

  // Contract for UI: read-only, safe from message thread
  std::shared_ptr<const PreparedState> getPreparedState() const;
  LoadStatus getLoadStatus() const;
  juce::String getLoadedSampleDisplayName() const;
  juce::String getLoadedSamplePath() const;
  int getNumSlices() const;
  juce::String getLoadErrorText() const;

  juce::AudioProcessorValueTreeState& getValueTreeState() { return apvts; }
  const juce::AudioProcessorValueTreeState& getValueTreeState() const { return apvts; }

  /** Called from UI thread. Starts async load; status becomes Loading then Ready/Error. */
  void loadSampleFromFile(const juce::File& file);
  /** Clear sample and set status to Idle. */
  void clearSample();
  /** Regenerate slice map for current BPM/granularity/seed (async). */
  void regenerateSliceMap();

  /** Rearrange the sample: shuffle slice order and write it into the buffer (waveform updates). */
  void rearrangeSample();

  /** Move selected slices in playback order and update waveform. direction: -1 = left, +1 = right.
   *  selectedPositions = visual slice indices (positions) the user selected. Returns new order if changed. */
  std::vector<size_t> moveSelectedSlicesInOrder(const std::unordered_set<size_t>& selectedPositions,
                                                int direction);

  /** Play the loaded sample from the start for 5 seconds (from UI). */
  void startPreview();

  /** Window range (startSample, endSample) snapped to slice boundaries (and file start/end). */
  std::pair<juce::int64, juce::int64> getWindowRangeSnappedToSlices() const;

  /** Last triggered slice index (for UI readout); written from audio thread, read from UI. */
  std::atomic<int> lastTriggeredSliceIndex{-1};

private:
  void applyNewPreparedState(std::shared_ptr<const PreparedState> state);
  void setLoadStatus(LoadStatus s);
  void setLoadError(const juce::String& text);
  void buildPreparedStateFromBuffer(juce::AudioBuffer<float> buffer, double sampleRate,
                                    const juce::String& displayName, const juce::String& path);

  juce::AudioProcessorValueTreeState apvts;

  mutable std::mutex preparedStateMutex_;
  std::shared_ptr<const PreparedState> preparedState_;
  std::atomic<LoadStatus> loadStatus_{LoadStatus::Idle};
  juce::String loadErrorText_;
  juce::String loadedSampleDisplayName_;
  juce::String loadedSamplePath_;
  mutable std::shared_mutex stateMutex_;

  juce::ThreadPool loadPool_{1};
  std::atomic<uint64_t> loadJobId_{0};

  double hostSampleRate_{44100.0};
  int hostBlockSize_{0};

  struct Voice {
    int sliceIndex{-1};
    size_t samplePosition{0};
    bool active{false};
    int noteId{0};
    float gain{1.f};
  };
  std::vector<Voice> voices_;
  static constexpr int kMaxVoices = 16;

  static constexpr double kPreviewLengthSeconds = 5.0;
  std::atomic<bool> previewActive_{false};
  std::atomic<juce::int64> previewSamplePos_{0};
  std::atomic<juce::int64> previewStartSample_{0};
  std::atomic<juce::int64> previewLengthSamples_{0};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerPluginProcessor)
};
