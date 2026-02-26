#include "PluginEditor.h"

SlicerPluginEditor::SlicerPluginEditor(SlicerPluginProcessor& p)
  : AudioProcessorEditor(&p), processorRef(p), topBar_(p), waveformView_(p),
    controlPanel_(p), midiMappingOverlay_(p)
{
  addAndMakeVisible(topBar_);
  addAndMakeVisible(waveformView_);
  controlPanelViewport_.setViewedComponent(&controlPanel_, false);
  controlPanelViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(controlPanelViewport_);
  addAndMakeVisible(midiMappingOverlay_);

  topBar_.setOnLoadClicked([this]()
  {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load sample", lastLoadedPath_.existsAsFile() ? lastLoadedPath_ : juce::File{},
        "*.wav;*.aiff;*.aif");
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& c)
        {
          if (c.getResults().isEmpty())
            return;
          juce::File f = c.getResult();
          lastLoadedPath_ = f;
          processorRef.loadSampleFromFile(f);
        });
  });
  topBar_.setOnResetClicked([this]()
  {
    juce::File path(processorRef.getLoadedSamplePath());
    if (path.existsAsFile())
      processorRef.loadSampleFromFile(path);
  });
  topBar_.setOnPreviewClicked([this]() { processorRef.startPreview(); });
  topBar_.setOnRearrangeClicked([this]() { processorRef.rearrangeSample(); });

  processorRef.getValueTreeState().addParameterListener("bpm", this);
  processorRef.getValueTreeState().addParameterListener("granularity", this);
  processorRef.getValueTreeState().addParameterListener("seed", this);

  startTimerHz(20);
  setSize(720, 420);
}

SlicerPluginEditor::~SlicerPluginEditor()
{
  processorRef.getValueTreeState().removeParameterListener("bpm", this);
  processorRef.getValueTreeState().removeParameterListener("granularity", this);
  processorRef.getValueTreeState().removeParameterListener("seed", this);
  stopTimer();
}

void SlicerPluginEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void SlicerPluginEditor::resized()
{
  auto r = getLocalBounds().reduced(4);
  const int topH = 36;
  const int bottomH = 24;
  const int panelW = 220;

  topBar_.setBounds(r.removeFromTop(topH));
  r.removeFromTop(4);
  midiMappingOverlay_.setBounds(r.removeFromBottom(bottomH));
  r.removeFromBottom(4);
  auto rightArea = r.removeFromRight(panelW);
  r.removeFromRight(4);
  controlPanelViewport_.setBounds(rightArea);
  const int contentHeight = 380;
  controlPanel_.setSize(rightArea.getWidth(), contentHeight);
  waveformView_.setBounds(r);
}

void SlicerPluginEditor::timerCallback()
{
  topBar_.refresh();
  waveformView_.refresh();
  midiMappingOverlay_.refresh();

  if (regenerateScheduledAt_ > 0 && juce::Time::getMillisecondCounter() >= regenerateScheduledAt_)
  {
    regenerateScheduledAt_ = 0;
    processorRef.regenerateSliceMap();
  }
}

void SlicerPluginEditor::parameterChanged(const juce::String& id, float)
{
  if (id == "bpm" || id == "granularity" || id == "seed")
    scheduleRegenerateSliceMap();
}

void SlicerPluginEditor::scheduleRegenerateSliceMap()
{
  regenerateScheduledAt_ = juce::Time::getMillisecondCounter() + 150;
}
