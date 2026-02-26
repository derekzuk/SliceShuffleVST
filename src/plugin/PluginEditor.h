#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "TopBarComponent.h"
#include "WaveformView.h"
#include "ControlPanel.h"
#include "MidiMappingOverlay.h"

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

private:
  void timerCallback() override;
  void parameterChanged(const juce::String& id, float) override;

  void scheduleRegenerateSliceMap();

  SlicerPluginProcessor& processorRef;
  TopBarComponent topBar_;
  WaveformView waveformView_;
  juce::Viewport controlPanelViewport_;
  ControlPanel controlPanel_;
  MidiMappingOverlay midiMappingOverlay_;
  juce::File lastLoadedPath_;
  juce::int64 regenerateScheduledAt_{0};
};
