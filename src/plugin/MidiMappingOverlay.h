#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MidiMappingOverlay : public juce::Component
{
public:
  explicit MidiMappingOverlay(SlicerPluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void refresh();

private:
  SlicerPluginProcessor& processor_;
};
