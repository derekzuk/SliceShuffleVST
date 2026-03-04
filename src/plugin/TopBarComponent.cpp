#include "TopBarComponent.h"

TopBarComponent::TopBarComponent(SliceShufflePluginProcessor& proc) : processor_(proc)
{
  loadButton_.setButtonText("Load Sample");
  addAndMakeVisible(loadButton_);
  loadButton_.getProperties().set (juce::Identifier ("variant"), juce::var ("secondary"));
  resetButton_.setButtonText("Reset");
  addAndMakeVisible(resetButton_);
  resetButton_.getProperties().set (juce::Identifier ("variant"), juce::var ("destructive"));
  sampleLabel_.setText("No sample loaded", juce::dontSendNotification);
  sampleLabel_.setJustificationType(juce::Justification::centredLeft);
  addAndMakeVisible(sampleLabel_);
  statusLabel_.setJustificationType(juce::Justification::centredRight);
  addAndMakeVisible(statusLabel_);
}

void TopBarComponent::setOnLoadClicked(std::function<void()> cb)
{
  onLoadClicked_ = std::move(cb);
  loadButton_.onClick = [this]() { if (onLoadClicked_) onLoadClicked_(); };
}

void TopBarComponent::setOnResetClicked(std::function<void()> cb)
{
  onResetClicked_ = std::move(cb);
  resetButton_.onClick = [this]() { if (onResetClicked_) onResetClicked_(); };
}

void TopBarComponent::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.1f));
}

void TopBarComponent::resized()
{
  auto r = getLocalBounds().reduced(4);
  const int butW = 100;
  loadButton_.setBounds(r.removeFromLeft(butW).reduced(2));
  r.removeFromLeft(8);
  resetButton_.setBounds(r.removeFromLeft(60).reduced(2));
  r.removeFromLeft(8);
  statusLabel_.setBounds(r.removeFromRight(90).reduced(2));
  sampleLabel_.setBounds(r.reduced(2));
}

void TopBarComponent::refresh()
{
  const auto status = processor_.getLoadStatus();
  juce::String statusText;
  juce::Colour statusColour;
  switch (status)
  {
  case SliceShufflePluginProcessor::LoadStatus::Idle:
    statusText = "";
    statusColour = juce::Colours::grey;
    break;
  case SliceShufflePluginProcessor::LoadStatus::Loading:
    statusText = "Loading...";
    statusColour = juce::Colours::orange;
    break;
  case SliceShufflePluginProcessor::LoadStatus::Ready:
    statusText = "Ready";
    statusColour = juce::Colours::green;
    break;
  case SliceShufflePluginProcessor::LoadStatus::Missing:
    statusText = "Missing";
    statusColour = juce::Colours::red;
    break;
  case SliceShufflePluginProcessor::LoadStatus::Error:
    statusText = "Error";
    statusColour = juce::Colours::red;
    break;
  }
  statusLabel_.setText(statusText, juce::dontSendNotification);
  statusLabel_.setColour(juce::Label::textColourId, statusColour);

  juce::String name = processor_.getLoadedSampleDisplayName();
  if (name.isEmpty())
    name = "No sample loaded";
  sampleLabel_.setText(name, juce::dontSendNotification);

  if (status == SliceShufflePluginProcessor::LoadStatus::Error)
  {
    juce::String err = processor_.getLoadErrorText();
    if (err.isNotEmpty())
      sampleLabel_.setText(name + " - " + err, juce::dontSendNotification);
  }
}
