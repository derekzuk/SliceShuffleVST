#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SlicerPluginEditor : public juce::AudioProcessorEditor
{
public:
  explicit SlicerPluginEditor(SlicerPluginProcessor&);
  ~SlicerPluginEditor() override;

  void paint(juce::Graphics&) override;
  void resized() override;

private:
  SlicerPluginProcessor& processorRef;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SlicerPluginEditor)
};
