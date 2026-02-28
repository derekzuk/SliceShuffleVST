#include "ControlPanel.h"

namespace {
constexpr const char* kBpmId = "bpm";
constexpr const char* kGranularityId = "granularity";
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
} // namespace

ControlPanel::ControlPanel(SlicerPluginProcessor& proc) : processor_(proc)
{
  bpmLabel_.setText("BPM", juce::dontSendNotification);
  addAndMakeVisible(bpmLabel_);
  bpmSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  bpmSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
  addAndMakeVisible(bpmSlider_);

  granularityLabel_.setText("Granularity", juce::dontSendNotification);
  addAndMakeVisible(granularityLabel_);
  granularityCombo_.addItem("1/4 beat", 1);
  granularityCombo_.addItem("1/2 beat", 2);
  granularityCombo_.addItem("1 beat", 3);
  granularityCombo_.addItem("2 beats", 4);
  granularityCombo_.addItem("4 beats", 5);
  granularityCombo_.setSelectedId(3); // "1 beat"
  addAndMakeVisible(granularityCombo_);

  windowLabel_.setText("Window Size", juce::dontSendNotification);
  addAndMakeVisible(windowLabel_);
  windowSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  windowSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
  addAndMakeVisible(windowSlider_);

  windowPositionLabel_.setText("Window Position", juce::dontSendNotification);
  addAndMakeVisible(windowPositionLabel_);
  windowPositionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  windowPositionSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
  addAndMakeVisible(windowPositionSlider_);

  auto& apvts = processor_.getValueTreeState();
  bpmAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      apvts, kBpmId, bpmSlider_);
  granularityAttachment_ =
      std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
          apvts, kGranularityId, granularityCombo_);
  windowAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      apvts, kWindowBeatsId, windowSlider_);
  windowPositionAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
      apvts, kWindowPositionId, windowPositionSlider_);
}

void ControlPanel::resized()
{
  auto r = getLocalBounds().reduced(4);
  const int rowH = 28;
  const int labelW = 72;

  auto row = [&]() { return r.removeFromTop(rowH); };
  auto leftLabel = [&](juce::Label& l, int w = labelW)
  {
    l.setBounds(row().removeFromLeft(w).reduced(2));
  };

  leftLabel(bpmLabel_);
  bpmSlider_.setBounds(row().reduced(2));
  r.removeFromTop(4);

  leftLabel(granularityLabel_);
  granularityCombo_.setBounds(row().reduced(2));
  r.removeFromTop(4);

  leftLabel(windowLabel_, 88);   // "Window Size"
  windowSlider_.setBounds(row().reduced(2));
  r.removeFromTop(4);

  leftLabel(windowPositionLabel_, 105);   // "Window Position"
  windowPositionSlider_.setBounds(row().reduced(2));
  r.removeFromTop(4);
}

void ControlPanel::refreshReseedFromProcessor() {}
