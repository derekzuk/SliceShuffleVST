#include "PluginEditor.h"
#include "../cli/WavWriter.h"

namespace {
const juce::Identifier kCutShuffleExportDragType("cutshuffle-export");
} // namespace

CutShufflePluginEditor::CutShufflePluginEditor(CutShufflePluginProcessor& p)
  : AudioProcessorEditor(&p), processorRef(p), topBar_(p), waveformOverview_(p), waveformView_(p), controlPanel_(p)
{
  addAndMakeVisible(topBar_);
  addAndMakeVisible(waveformOverview_);
  addAndMakeVisible(waveformView_);
  controlPanelViewport_.setViewedComponent(&controlPanel_, false);
  controlPanelViewport_.setScrollBarsShown(false, false);
  addAndMakeVisible(controlPanelViewport_);
  rearrangeButton_.setButtonText("Rearrange");
  addAndMakeVisible(rearrangeButton_);
  rearrangeButton_.onClick = [this]() { processorRef.rearrangeSample(); };
  previewButton_.setButtonText("Preview");
  addAndMakeVisible(previewButton_);
  previewButton_.onClick = [this]()
  {
    if (processorRef.isPreviewActive())
      processorRef.stopPreview();
    else
    {
      processorRef.startPreview();
      waveformView_.grabKeyboardFocus();
    }
  };
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
    processorRef.resetArrangement();
    waveformView_.clearSelection();
  });

  processorRef.getValueTreeState().addParameterListener("bpm", this);
  processorRef.getValueTreeState().addParameterListener("granularity", this);

  lastGranularityIndex_ = static_cast<int>(std::round(
      processorRef.getValueTreeState().getRawParameterValue("granularity")->load()));

  startTimerHz(20);
  setSize(720, 420);
}

CutShufflePluginEditor::~CutShufflePluginEditor()
{
  processorRef.getValueTreeState().removeParameterListener("bpm", this);
  processorRef.getValueTreeState().removeParameterListener("granularity", this);
  stopTimer();
}

void CutShufflePluginEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void CutShufflePluginEditor::resized()
{
  auto r = getLocalBounds().reduced(4);
  const int topH = 36;
  const int bottomH = 28;
  const int panelW = 220;

  topBar_.setBounds(r.removeFromTop(topH));
  r.removeFromTop(4);
  const int overviewHeight = 28;
  waveformOverview_.setBounds(r.removeFromTop(overviewHeight));
  r.removeFromTop(2);
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

void CutShufflePluginEditor::timerCallback()
{
  topBar_.refresh();
  waveformOverview_.ensureEnvelopeBuilt();
  waveformOverview_.repaint();
  waveformView_.refresh();
  previewButton_.setButtonText(processorRef.isPreviewActive() ? "Stop" : "Preview");

  if (regenerateScheduledAt_ > 0 && juce::Time::getMillisecondCounter() >= regenerateScheduledAt_)
  {
    regenerateScheduledAt_ = 0;
    processorRef.regenerateSliceMap();
  }
}

void CutShufflePluginEditor::parameterChanged(const juce::String& id, float)
{
  if (id == "bpm")
  {
    scheduleRegenerateSliceMap();
    return;
  }
  if (id == "granularity")
  {
    if (revertingGranularity_)
      return;
    const int newIndex = static_cast<int>(std::round(
        processorRef.getValueTreeState().getRawParameterValue("granularity")->load()));
    if (processorRef.isLoadingPreset())
    {
      lastGranularityIndex_ = newIndex;
      return;
    }
    const int previousIndex = lastGranularityIndex_;
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel(
        juce::MessageBoxIconType::InfoIcon,
        "Granularity changed",
        "Changing granularity resets the slice arrangement to the original order.",
        "OK",
        "Cancel",
        this);
    juce::AlertWindow::showAsync(options, [this, previousIndex, newIndex](int buttonIndex)
    {
      // Button order is platform-dependent: 0 may be Cancel, 1 may be OK (or vice versa).
      const bool confirmed = (buttonIndex == 1);
      if (confirmed)
      {
        revertingGranularity_ = true;
        processorRef.getValueTreeState().getParameterAsValue("granularity").setValue(newIndex);
        revertingGranularity_ = false;
        lastGranularityIndex_ = newIndex;
        waveformView_.clearSelection();
        scheduleRegenerateSliceMap();
      }
      else
      {
        revertingGranularity_ = true;
        processorRef.getValueTreeState().getParameterAsValue("granularity").setValue(previousIndex);
        revertingGranularity_ = false;
      }
    });
  }
}

void CutShufflePluginEditor::scheduleRegenerateSliceMap()
{
  regenerateScheduledAt_ = juce::Time::getMillisecondCounter() + 150;
}

bool CutShufflePluginEditor::writeWindowToWavFile(const juce::File& file) const
{
  auto state = processorRef.getPreparedState();
  if (!state || state->buffer.getNumSamples() == 0 || state->sampleRate <= 0)
    return false;
  juce::AudioBuffer<float> windowBuffer;
  processorRef.renderWindowToBuffer(*state, windowBuffer);
  if (windowBuffer.getNumSamples() <= 0)
    return false;
  return writeWav(file, windowBuffer, state->sampleRate);
}

bool CutShufflePluginEditor::shouldDropFilesWhenDraggedExternally(
    const juce::DragAndDropTarget::SourceDetails& sourceDetails,
    juce::StringArray& files,
    bool& canMoveFiles)
{
  if (sourceDetails.description != juce::var(kCutShuffleExportDragType.toString()))
    return false;
  juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
  juce::File outFile = tempDir.getChildFile("CutShuffle_export_" + juce::Uuid().toDashedString() + ".wav");
  if (!writeWindowToWavFile(outFile))
    return false;
  files.add(outFile.getFullPathName());
  canMoveFiles = false;
  return true;
}
