#pragma once

#include <JuceHeader.h>
#include "PreparedState.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

class SliceShufflePluginProcessor : public juce::AudioProcessor
{
public:
  enum class LoadStatus { Idle, Loading, Ready, Missing, Error };

  SliceShufflePluginProcessor();
  ~SliceShufflePluginProcessor() override;

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

  /** Save current plugin state to an external preset file. */
  bool savePresetToFile(const juce::File& file) const;
  /** Load plugin state from an external preset file. */
  bool loadPresetFromFile(const juce::File& file);

  /** True while loadPresetFromFile is restoring state (editor uses this to suppress granularity dialog). */
  bool isLoadingPreset() const { return loadingPreset_.load(); }

  /** Reset slice playback order to identity for the current sample. */
  void resetArrangement();

  /** Render the current window region (respecting playbackOrder) into out. */
  void renderWindowToBuffer(const PreparedState& state, juce::AudioBuffer<float>& out) const;

  /** Render sample range [startSample, endSample) in playback order (for live overview drag). */
  void renderSampleRangeToBuffer(const PreparedState& state, juce::int64 startSample, juce::int64 endSample,
                                 juce::AudioBuffer<float>& out) const;

  /** Called from UI thread. Starts async load; status becomes Loading then Ready/Error.
   *  If restoreArrangement is true (e.g. preset load), applies pending playback order and muted positions and does not reset window position. */
  void loadSampleFromFile(const juce::File& file, bool restoreArrangement = false);
  /** Clear sample and set status to Idle. */
  void clearSample();
  /** Regenerate slice map for current BPM/granularity/seed (async). */
  void regenerateSliceMap();

  /** Rearrange the sample: shuffle slice order and write it into the buffer (waveform updates).
   *  When selectedPositions is non-empty, only those logical positions are shuffled among themselves;
   *  when empty (or no-arg), shuffles all slices in the current window. */
  void rearrangeSample();
  void rearrangeSample(const std::unordered_set<size_t>& selectedPositions);

  /** Move selected slices in playback order and update waveform. direction: -1 = left, +1 = right.
   *  selectedPositions = visual slice indices (positions) the user selected. Returns new order if changed. */
  std::vector<size_t> moveSelectedSlicesInOrder(const std::unordered_set<size_t>& selectedPositions,
                                                int direction);

  /** Toggle silence (non-destructive mute) for the given logical positions (indices into playbackOrder). */
  void silenceSelectedSlices(const std::unordered_set<size_t>& selectedPositions);

  /** Duplicate selected logical positions and insert the duplicates immediately to the right of the
   *  rightmost selected position. Overall number of logical positions (playbackOrder.size()) is
   *  kept constant; duplicates overwrite slices at the far right of the sequence if needed. */
  void duplicateSelectedSlices(const std::unordered_set<size_t>& selectedPositions);

  /** Remove selected logical positions from the arrangement. Slices to the right shift left.
   *  playbackOrder shrinks; muted positions are remapped to the new indices. */
  void removeSelectedSlices(const std::unordered_set<size_t>& selectedPositions);

  /** Toggle reverse playback for selected slices (audio plays backwards; waveform display updates). */
  void reverseSelectedSlices(const std::unordered_set<size_t>& selectedPositions);

  /** Play the loaded sample from the start for 5 seconds (from UI). */
  void startPreview();
  /** Stop preview playback (from UI). */
  void stopPreview();
  /** True while preview is playing. */
  bool isPreviewActive() const;

  /** Window range (startSample, endSample) snapped to slice boundaries (and file start/end). */
  std::pair<juce::int64, juce::int64> getWindowRangeSnappedToSlices() const;
  std::pair<juce::int64, juce::int64> getWindowRangeSnappedToSlices(const PreparedState& state) const;

  /** Logical position range [min, max] (inclusive) of the current window. Positions outside the window are not visible in the bottom view. */
  std::pair<size_t, size_t> getWindowLogicalPositionRange(const PreparedState& state) const;

  /** Last triggered slice index (for UI readout); written from audio thread, read from UI. */
  std::atomic<int> lastTriggeredSliceIndex{-1};

  /** Undo last arrangement change (playback order). Safe to call from message thread.
   *  Pass current selection so it can be stored for redo; after return, call takePendingRestoreSelection() and apply to view if present. */
  void undo(const std::unordered_set<size_t>& currentSelection);
  /** Redo last undone arrangement change. Pass current selection for same reason. */
  void redo(const std::unordered_set<size_t>& currentSelection);
  bool canUndo() const;
  bool canRedo() const;
  /** If undo/redo restored a selection, returns it and clears; otherwise returns nullopt. Call from message thread after undo()/redo(). */
  std::optional<std::unordered_set<size_t>> takePendingRestoreSelection();

private:
  void applyNewPreparedState(std::shared_ptr<const PreparedState> state);
  void setLoadStatus(LoadStatus s);
  void setLoadError(const juce::String& text);
  void buildPreparedStateFromBuffer(juce::AudioBuffer<float> buffer, double sampleRate,
                                    const juce::String& displayName, const juce::String& path,
                                    bool forceIdentityOrder = false);

  std::unique_ptr<juce::XmlElement> createFullStateXml() const;
  void restoreStateFromXml(const juce::XmlElement& xml, bool restoreSamplePath = false);

  // Helpers for non-destructive rearrange/preview.
  std::pair<size_t, size_t> getWindowSliceRange(const PreparedState& state) const;

  // Undo/redo for arrangement (playback order) and optionally mute/reversed state. gen_ invalidates entries on load/regenerate.
  struct UndoEntry {
    uint64_t gen{0};
    std::vector<size_t> order;
    std::optional<std::unordered_set<size_t>> selectionToRestore;
    std::optional<std::unordered_set<size_t>> mutedToRestore;
    std::optional<std::unordered_set<size_t>> reversedToRestore;
  };
  static constexpr size_t kMaxUndoSteps = 50;
  std::deque<UndoEntry> undo_;
  std::deque<UndoEntry> redo_;
  uint64_t gen_{0};
  std::optional<std::unordered_set<size_t>> pendingRestoreSelection_;
  void pushUndoEntry(std::vector<size_t> currentOrder,
                    std::optional<std::unordered_set<size_t>> selectionToRestore = std::nullopt,
                    std::optional<std::unordered_set<size_t>> mutedToRestore = std::nullopt,
                    std::optional<std::unordered_set<size_t>> reversedToRestore = std::nullopt);
  void incrementGeneration();

  juce::AudioProcessorValueTreeState apvts;

  mutable std::mutex preparedStateMutex_;
  std::shared_ptr<const PreparedState> preparedState_;
  std::atomic<LoadStatus> loadStatus_{LoadStatus::Idle};
  juce::String loadErrorText_;
  juce::String loadedSampleDisplayName_;
  juce::String loadedSamplePath_;
  mutable std::shared_mutex stateMutex_;

  // When restoring from saved state we may have a custom playback order,
  // muted positions, and reversed positions to apply after the sample has been (re)loaded.
  std::vector<size_t> pendingPlaybackOrder_;
  std::unordered_set<size_t> pendingMutedLogicalPositions_;
  std::unordered_set<size_t> pendingReversedLogicalPositions_;

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
    bool reversed{false};
  };
  std::vector<Voice> voices_;
  static constexpr int kMaxVoices = 16;

  static constexpr double kPreviewLengthSeconds = 5.0;
  std::atomic<bool> previewActive_{false};
  /** Playback position in source (file) sample space; advanced by previewSourceToHostRatio_ per output sample. */
  std::atomic<float> previewPlaybackPos_{0.f};
  /** Source samples per host output sample (fileSampleRate / hostSampleRate). */
  std::atomic<float> previewSourceToHostRatio_{1.f};
  std::atomic<juce::int64> previewLengthSamples_{0};

  // Pre-rendered buffer for preview (window, at file sample rate).
  juce::AudioBuffer<float> previewBuffer_;

  std::atomic<bool> loadingPreset_{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliceShufflePluginProcessor)
};
