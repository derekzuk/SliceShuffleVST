#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TopBarComponent.h"
#include "WaveformView.h"
#include "WaveformOverview.h"
#include "ControlPanel.h"

class SlicerPluginEditor : public juce::AudioProcessorEditor,
                           public juce::DragAndDropContainer,
                           private juce::Timer,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
  explicit SlicerPluginEditor(SlicerPluginProcessor&);
  ~SlicerPluginEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

  bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& sourceDetails,
                                            juce::StringArray& files,
                                            bool& canMoveFiles) override;

  /** Write current window region to a WAV file. Returns true on success. */
  bool writeWindowToWavFile(const juce::File& file) const;

private:
  void timerCallback() override;
  void parameterChanged(const juce::String& id, float) override;

  void scheduleRegenerateSliceMap();

  SlicerPluginProcessor& processorRef;
  TopBarComponent topBar_;
  WaveformOverview waveformOverview_;
  WaveformView waveformView_;
  juce::Viewport controlPanelViewport_;
  ControlPanel controlPanel_;
  juce::TextButton rearrangeButton_;
  juce::TextButton previewButton_;
  juce::TextButton exportButton_;
  juce::File lastLoadedPath_;
  juce::int64 regenerateScheduledAt_{0};
  int lastGranularityIndex_{2};  // 2 = "1 beat" (ComboBox selectedId 3, param value 2)
  bool revertingGranularity_{false};
};
