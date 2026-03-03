#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ControlPanel : public juce::Component
{
public:
  explicit ControlPanel(SliceShufflePluginProcessor& proc);

  void resized() override;

  void refreshReseedFromProcessor();

private:
  SliceShufflePluginProcessor& processor_;
  juce::Slider bpmSlider_;
  juce::ComboBox granularityCombo_;
  juce::Slider windowSlider_;
  juce::Label bpmLabel_;
  juce::Label granularityLabel_;
  juce::Label windowLabel_;

  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> granularityAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowAttachment_;
};
