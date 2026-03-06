#include "PluginEditor.h"
#include "../cli/WavWriter.h"

namespace {
const juce::Identifier kSliceShuffleExportDragType("sliceshuffle-export");
} // namespace

SliceShufflePluginEditor::SliceShufflePluginEditor(SliceShufflePluginProcessor& p)
  : AudioProcessorEditor(&p), processorRef(p), topBar_(p), waveformOverview_(p), waveformView_(p), controlPanel_(p)
{
  addAndMakeVisible(topBar_);
  addAndMakeVisible(waveformOverview_);
  addAndMakeVisible(waveformView_);
  waveformOverview_.setLiveWindowRangeCallback(
      [this](juce::int64 startSample, juce::int64 endSample)
      { waveformView_.setDisplayWindowOverride(startSample, endSample); });
  setWantsKeyboardFocus(true);
  controlPanelViewport_.setViewedComponent(&controlPanel_, false);
  controlPanelViewport_.setScrollBarsShown(false, false);
  addAndMakeVisible(controlPanelViewport_);
  undoButton_.setButtonText("Undo");
  addAndMakeVisible(undoButton_);
  undoButton_.onClick = [this]()
  {
    processorRef.undo(waveformView_.getSelectedSliceIndices());
    if (auto sel = processorRef.takePendingRestoreSelection())
      waveformView_.setSelectedSliceIndices(std::move(*sel));
  };
  redoButton_.setButtonText("Redo");
  addAndMakeVisible(redoButton_);
  redoButton_.onClick = [this]()
  {
    processorRef.redo(waveformView_.getSelectedSliceIndices());
    if (auto sel = processorRef.takePendingRestoreSelection())
      waveformView_.setSelectedSliceIndices(std::move(*sel));
  };
  rearrangeButton_.setButtonText("Shuffle");
  addAndMakeVisible(rearrangeButton_);
  rearrangeButton_.onClick = [this]()
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
      processorRef.rearrangeSample(sel);
    else
      processorRef.rearrangeSample();
  };
  silenceButton_.setButtonText("Silence");
  addAndMakeVisible(silenceButton_);
  silenceButton_.onClick = [this]()
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
      processorRef.silenceSelectedSlices(sel);
  };
  duplicateButton_.setButtonText("Duplicate");
  addAndMakeVisible(duplicateButton_);
  duplicateButton_.onClick = [this]()
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
      processorRef.duplicateSelectedSlices(sel);
  };
  deleteButton_.setButtonText("Delete");
  addAndMakeVisible(deleteButton_);
  deleteButton_.onClick = [this]()
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
    {
      processorRef.removeSelectedSlices(sel);
      waveformView_.clearSelection();
    }
  };
  reverseButton_.setButtonText("Reverse");
  addAndMakeVisible(reverseButton_);
  reverseButton_.onClick = [this]()
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
      processorRef.reverseSelectedSlices(sel);
  };
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

  setLookAndFeel (&sliceShuffleLf_);
  undoButton_.getProperties().set ("variant", juce::var ("tertiary"));
  redoButton_.getProperties().set ("variant", juce::var ("tertiary"));
  rearrangeButton_.getProperties().set ("variant", juce::var ("primary"));
  silenceButton_.getProperties().set ("variant", juce::var ("secondary"));
  duplicateButton_.getProperties().set ("variant", juce::var ("secondary"));
  deleteButton_.getProperties().set ("variant", juce::var ("destructive"));
  reverseButton_.getProperties().set ("variant", juce::var ("secondary"));
  previewButton_.getProperties().set ("variant", juce::var ("secondary"));
  previewButton_.setClickingTogglesState (true);
  exportButton_.getProperties().set ("variant", juce::var ("secondary"));

  topBar_.setOnLoadClicked([this]()
  {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load sample", lastLoadedPath_.existsAsFile() ? lastLoadedPath_ : juce::File{},
        "*.wav;*.mp3;*.m4a;*.aiff;*.aif");
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

SliceShufflePluginEditor::~SliceShufflePluginEditor()
{
  setLookAndFeel (nullptr);
  processorRef.getValueTreeState().removeParameterListener("bpm", this);
  processorRef.getValueTreeState().removeParameterListener("granularity", this);
  stopTimer();
}

void SliceShufflePluginEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

bool SliceShufflePluginEditor::keyPressed(const juce::KeyPress& key)
{
  if (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey)
  {
    const auto& sel = waveformView_.getSelectedSliceIndices();
    if (!sel.empty())
    {
      processorRef.removeSelectedSlices(sel);
      waveformView_.clearSelection();
      return true;
    }
  }
  if (key == juce::KeyPress::spaceKey)
  {
    if (processorRef.isPreviewActive())
      processorRef.stopPreview();
    else
    {
      processorRef.startPreview();
      waveformView_.grabKeyboardFocus();
    }
    return true;
  }
  if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
  {
    if (key.getKeyCode() == 'z')
    {
      if (key.getModifiers().isShiftDown())
      {
        if (processorRef.canRedo())
        {
          processorRef.redo(waveformView_.getSelectedSliceIndices());
          if (auto sel = processorRef.takePendingRestoreSelection())
            waveformView_.setSelectedSliceIndices(std::move(*sel));
          return true;
        }
      }
      else
      {
        if (processorRef.canUndo())
        {
          processorRef.undo(waveformView_.getSelectedSliceIndices());
          if (auto sel = processorRef.takePendingRestoreSelection())
            waveformView_.setSelectedSliceIndices(std::move(*sel));
          return true;
        }
      }
    }
  }
  return false;
}

void SliceShufflePluginEditor::resized()
{
  auto r = getLocalBounds().reduced(4);
  const int topH = 36;
  const int bottomH = sliceshuffle::UiTokens::bottomBarHeight;
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

  const int tertiaryW = sliceshuffle::UiTokens::tertiaryButtonWidth;
  const int secondaryW = sliceshuffle::UiTokens::secondaryButtonWidth;
  const int primaryW = secondaryW + sliceshuffle::UiTokens::primaryButtonWidthExtra;
  const int gap = sliceshuffle::UiTokens::buttonGap;
  const int groupGap = sliceshuffle::UiTokens::groupGap;

  // Left group: Undo, Redo (tertiary)
  auto leftGroup = bottomBar.removeFromLeft (tertiaryW + gap + tertiaryW);
  undoButton_.setBounds (leftGroup.removeFromLeft (tertiaryW).reduced (2));
  leftGroup.removeFromLeft (gap);
  redoButton_.setBounds (leftGroup.removeFromLeft (tertiaryW).reduced (2));

  bottomBar.removeFromLeft (groupGap);

  // Middle group: Shuffle (primary), Silence, Duplicate, Delete, Reverse (secondary / destructive)
  auto midGroup = bottomBar.removeFromLeft (primaryW + gap + secondaryW + gap + secondaryW + gap + secondaryW + gap + secondaryW);
  rearrangeButton_.setBounds (midGroup.removeFromLeft (primaryW).reduced (2));
  midGroup.removeFromLeft (gap);
  silenceButton_.setBounds (midGroup.removeFromLeft (secondaryW).reduced (2));
  midGroup.removeFromLeft (gap);
  duplicateButton_.setBounds (midGroup.removeFromLeft (secondaryW).reduced (2));
  midGroup.removeFromLeft (gap);
  deleteButton_.setBounds (midGroup.removeFromLeft (secondaryW).reduced (2));
  midGroup.removeFromLeft (gap);
  reverseButton_.setBounds (midGroup.removeFromLeft (secondaryW).reduced (2));

  bottomBar.removeFromLeft (groupGap);

  // Right group: Preview, Export (secondary); Export rightmost
  auto rightGroup = bottomBar;
  previewButton_.setBounds (rightGroup.removeFromLeft (secondaryW).reduced (2));
  rightGroup.removeFromLeft (gap);
  exportButton_.setBounds (rightGroup.removeFromLeft (secondaryW).reduced (2));

  waveformView_.setBounds(r);
}

void SliceShufflePluginEditor::timerCallback()
{
  topBar_.refresh();
  waveformOverview_.ensureEnvelopeBuilt();
  waveformOverview_.repaint();
  waveformView_.refresh();
  previewButton_.setButtonText(processorRef.isPreviewActive() ? "Stop" : "Preview");
  previewButton_.setToggleState (processorRef.isPreviewActive(), juce::dontSendNotification);
  undoButton_.setEnabled(processorRef.canUndo());
  redoButton_.setEnabled(processorRef.canRedo());

  if (regenerateScheduledAt_ > 0 && juce::Time::getMillisecondCounter() >= regenerateScheduledAt_)
  {
    regenerateScheduledAt_ = 0;
    processorRef.regenerateSliceMap();
  }
}

void SliceShufflePluginEditor::parameterChanged(const juce::String& id, float)
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

void SliceShufflePluginEditor::scheduleRegenerateSliceMap()
{
  regenerateScheduledAt_ = juce::Time::getMillisecondCounter() + 150;
}

bool SliceShufflePluginEditor::writeWindowToWavFile(const juce::File& file) const
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

bool SliceShufflePluginEditor::shouldDropFilesWhenDraggedExternally(
    const juce::DragAndDropTarget::SourceDetails& sourceDetails,
    juce::StringArray& files,
    bool& canMoveFiles)
{
  if (sourceDetails.description != juce::var(kSliceShuffleExportDragType.toString()))
    return false;
  juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
  juce::File outFile = tempDir.getChildFile("SliceShuffle_export_" + juce::Uuid().toDashedString() + ".wav");
  if (!writeWindowToWavFile(outFile))
    return false;
  files.add(outFile.getFullPathName());
  canMoveFiles = false;
  return true;
}
