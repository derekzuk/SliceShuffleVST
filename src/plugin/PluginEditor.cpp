#include "PluginEditor.h"
#include "../cli/WavWriter.h"

namespace {
const juce::Identifier kSlicerExportDragType("slicer-export");
} // namespace

SlicerPluginEditor::SlicerPluginEditor(SlicerPluginProcessor& p)
  : AudioProcessorEditor(&p), processorRef(p), topBar_(p), waveformView_(p), controlPanel_(p)
{
  addAndMakeVisible(topBar_);
  addAndMakeVisible(waveformView_);
  controlPanelViewport_.setViewedComponent(&controlPanel_, false);
  controlPanelViewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(controlPanelViewport_);
  rearrangeButton_.setButtonText("Rearrange");
  addAndMakeVisible(rearrangeButton_);
  rearrangeButton_.onClick = [this]() { processorRef.rearrangeSample(); };
  previewButton_.setButtonText("Preview");
  addAndMakeVisible(previewButton_);
  previewButton_.onClick = [this]() { processorRef.startPreview(); };
  exportButton_.setButtonText("Export");
  addAndMakeVisible(exportButton_);
  exportButton_.onClick = [this]()
  {
    juce::File suggestedDir = lastLoadedPath_.existsAsFile() ? lastLoadedPath_.getParentDirectory() : juce::File();
    auto chooser = std::make_shared<juce::FileChooser>("Export window as WAV", suggestedDir, "*.wav");
    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, chooser](const juce::FileChooser& c)
        {
          if (c.getResults().isEmpty())
            return;
          juce::File f = c.getResult();
          if (f.getFileExtension().isEmpty())
            f = f.withFileExtension("wav");
          writeWindowToWavFile(f);
        });
  };

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
  const int bottomH = 28;
  const int panelW = 220;

  topBar_.setBounds(r.removeFromTop(topH));
  r.removeFromTop(4);
  auto bottomBar = r.removeFromBottom(bottomH);
  r.removeFromBottom(4);
  auto rightArea = r.removeFromRight(panelW);
  r.removeFromRight(4);
  controlPanelViewport_.setBounds(rightArea);
  controlPanel_.setSize(rightArea.getWidth(), 380);
  const int butW = 90;
  rearrangeButton_.setBounds(bottomBar.removeFromLeft(butW).reduced(2));
  bottomBar.removeFromLeft(4);
  previewButton_.setBounds(bottomBar.removeFromLeft(butW).reduced(2));
  bottomBar.removeFromLeft(4);
  exportButton_.setBounds(bottomBar.removeFromLeft(butW).reduced(2));
  waveformView_.setBounds(r);
}

void SlicerPluginEditor::timerCallback()
{
  topBar_.refresh();
  waveformView_.refresh();

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

bool SlicerPluginEditor::writeWindowToWavFile(const juce::File& file) const
{
  auto state = processorRef.getPreparedState();
  if (!state || state->buffer.getNumSamples() == 0 || state->sampleRate <= 0)
    return false;
  auto [startSample, endSample] = processorRef.getWindowRangeSnappedToSlices();
  const int lengthSamples = static_cast<int>(juce::jmax(juce::int64(0), endSample - startSample));
  if (lengthSamples <= 0)
    return false;
  const juce::AudioBuffer<float>& fullBuffer = state->buffer;
  juce::AudioBuffer<float> windowBuffer(fullBuffer.getNumChannels(), lengthSamples);
  const int start = static_cast<int>(juce::jlimit(juce::int64(0), static_cast<juce::int64>(fullBuffer.getNumSamples()), startSample));
  for (int ch = 0; ch < fullBuffer.getNumChannels(); ++ch)
    windowBuffer.copyFrom(ch, 0, fullBuffer, ch, start, lengthSamples);
  return writeWav(file, windowBuffer, state->sampleRate);
}

bool SlicerPluginEditor::shouldDropFilesWhenDraggedExternally(
    const juce::DragAndDropTarget::SourceDetails& sourceDetails,
    juce::StringArray& files,
    bool& canMoveFiles)
{
  if (sourceDetails.description != juce::var(kSlicerExportDragType.toString()))
    return false;
  juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
  juce::File outFile = tempDir.getChildFile("Slicer_export_" + juce::Uuid().toDashedString() + ".wav");
  if (!writeWindowToWavFile(outFile))
    return false;
  files.add(outFile.getFullPathName());
  canMoveFiles = false;
  return true;
}
