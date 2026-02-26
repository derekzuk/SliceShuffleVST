#include "ControlPanel.h"

namespace {
constexpr const char* kBpmId = "bpm";
constexpr const char* kGranularityId = "granularity";
constexpr const char* kSeedId = "seed";
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
  granularityCombo_.addItem("1/2 beat", 1);
  granularityCombo_.addItem("1 beat", 2);
  granularityCombo_.addItem("2 beats", 3);
  granularityCombo_.addItem("4 beats", 4);
  granularityCombo_.addItem("8 beats", 5);
  granularityCombo_.addItem("16 beats", 6);
  granularityCombo_.setSelectedId(2);
  addAndMakeVisible(granularityCombo_);

  windowLabel_.setText("Window (beats)", juce::dontSendNotification);
  addAndMakeVisible(windowLabel_);
  windowSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  windowSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
  addAndMakeVisible(windowSlider_);

  windowPositionLabel_.setText("Window position", juce::dontSendNotification);
  addAndMakeVisible(windowPositionLabel_);
  windowPositionSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
  windowPositionSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 22);
  addAndMakeVisible(windowPositionSlider_);

  reseedButton_.setButtonText("Reseed");
  addAndMakeVisible(reseedButton_);
  reseedButton_.onClick = [this]()
  {
    auto& apvts = processor_.getValueTreeState();
    juce::RangedAudioParameter* p = apvts.getParameter(kSeedId);
    if (p)
    {
      const int newSeed = juce::Random::getSystemRandom().nextInt(1000000);
      apvts.getParameterAsValue(kSeedId).setValue(newSeed);
      processor_.regenerateSliceMap();
    }
  };

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

  leftLabel(windowLabel_);
  windowSlider_.setBounds(row().reduced(2));
  r.removeFromTop(4);

  leftLabel(windowPositionLabel_);
  windowPositionSlider_.setBounds(row().reduced(2));
  r.removeFromTop(4);

  row();
  reseedButton_.setBounds(row().removeFromRight(80).reduced(2));
}

void ControlPanel::refreshReseedFromProcessor() {}
