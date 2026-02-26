#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class TopBarComponent : public juce::Component
{
public:
  explicit TopBarComponent(SlicerPluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void resized() override;

  void setOnLoadClicked(std::function<void()> cb);
  void setOnResetClicked(std::function<void()> cb);
  void setOnPreviewClicked(std::function<void()> cb);
  void setOnRearrangeClicked(std::function<void()> cb);
  void refresh();

private:
  SlicerPluginProcessor& processor_;
  juce::TextButton loadButton_;
  juce::TextButton resetButton_;
  juce::TextButton previewButton_;
  juce::TextButton rearrangeButton_;
  juce::Label sampleLabel_;
  juce::Label statusLabel_;
  std::function<void()> onLoadClicked_;
  std::function<void()> onResetClicked_;
  std::function<void()> onPreviewClicked_;
  std::function<void()> onRearrangeClicked_;
};
