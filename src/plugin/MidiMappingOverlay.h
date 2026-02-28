#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class MidiMappingOverlay : public juce::Component
{
public:
  explicit MidiMappingOverlay(CutShufflePluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void refresh();

private:
  CutShufflePluginProcessor& processor_;
};
