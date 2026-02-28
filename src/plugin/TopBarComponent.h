#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class TopBarComponent : public juce::Component
{
public:
  explicit TopBarComponent(CutShufflePluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void resized() override;

  void setOnLoadClicked(std::function<void()> cb);
  void setOnResetClicked(std::function<void()> cb);
  void refresh();

private:
  CutShufflePluginProcessor& processor_;
  juce::TextButton loadButton_;
  juce::TextButton resetButton_;
  juce::Label sampleLabel_;
  juce::Label statusLabel_;
  std::function<void()> onLoadClicked_;
  std::function<void()> onResetClicked_;
};
