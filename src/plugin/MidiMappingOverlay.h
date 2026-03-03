#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MidiMappingOverlay : public juce::Component
{
public:
  explicit MidiMappingOverlay(SliceShufflePluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void refresh();

private:
  SliceShufflePluginProcessor& processor_;
};
