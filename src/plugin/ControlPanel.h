#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ControlPanel : public juce::Component
{
public:
  explicit ControlPanel(CutShufflePluginProcessor& proc);

  void resized() override;

  void refreshReseedFromProcessor();

private:
  CutShufflePluginProcessor& processor_;
  juce::Slider bpmSlider_;
  juce::ComboBox granularityCombo_;
  juce::Slider windowSlider_;
  juce::Slider windowPositionSlider_;
  juce::Label bpmLabel_;
  juce::Label granularityLabel_;
  juce::Label windowLabel_;
  juce::Label windowPositionLabel_;

  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> granularityAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowAttachment_;
  std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> windowPositionAttachment_;
};
