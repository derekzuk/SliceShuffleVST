#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TopBarComponent.h"
#include "WaveformView.h"
#include "WaveformOverview.h"
#include "ControlPanel.h"
#include "SliceShuffleLookAndFeel.h"
#include "UiTokens.h"

class SliceShufflePluginEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
  explicit SliceShufflePluginEditor(SliceShufflePluginProcessor&);
  ~SliceShufflePluginEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  bool keyPressed(const juce::KeyPress& key) override;

  bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& sourceDetails,
                                            juce::StringArray& files,
                                            bool& canMoveFiles) override;

  /** Write current window region to a WAV file. Returns true on success. */
  bool writeWindowToWavFile(const juce::File& file) const;

private:
  void timerCallback() override;
  void parameterChanged(const juce::String& id, float) override;

  void scheduleRegenerateSliceMap();

  SliceShufflePluginProcessor& processorRef;
  sliceshuffle::SliceShuffleLookAndFeel sliceShuffleLf_;
  TopBarComponent topBar_;
  WaveformOverview waveformOverview_;
  WaveformView waveformView_;
  juce::Viewport controlPanelViewport_;
  ControlPanel controlPanel_;
  juce::TextButton rearrangeButton_;
  juce::TextButton silenceButton_;
  juce::TextButton duplicateButton_;
  juce::TextButton deleteButton_;
  juce::TextButton reverseButton_;
  juce::TextButton previewButton_;
  juce::TextButton exportButton_;
  juce::TextButton undoButton_;
  juce::TextButton redoButton_;
  juce::File lastLoadedPath_;
  juce::int64 regenerateScheduledAt_{0};
  int lastGranularityIndex_{1};  // 1 = "1/2 beat" default
  bool revertingGranularity_{false};
};
