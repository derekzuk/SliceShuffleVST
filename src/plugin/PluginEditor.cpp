#include "PluginEditor.h"
#include "../cli/WavWriter.h"

namespace {
const juce::Identifier kSliceShuffleExportDragType("sliceshuffle-export");
// Unicode: U+21BA = undo (↺), U+21BB = redo (↻)
const juce::String kUndoSymbol = juce::String::charToString(static_cast<juce::juce_wchar>(0x21BA));
const juce::String kRedoSymbol = juce::String::charToString(static_cast<juce::juce_wchar>(0x21BB));
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
  undoButton_.setButtonText(kUndoSymbol);
  addAndMakeVisible(undoButton_);
  undoButton_.onClick = [this]()
  {
    processorRef.undo(waveformView_.getSelectedSliceIndices());
    if (auto sel = processorRef.takePendingRestoreSelection())
      waveformView_.setSelectedSliceIndices(std::move(*sel));
  };
  redoButton_.setButtonText(kRedoSymbol);
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
  undoButton_.getProperties().set ("iconButton", juce::var (true));
  redoButton_.getProperties().set ("variant", juce::var ("tertiary"));
  redoButton_.getProperties().set ("iconButton", juce::var (true));
  rearrangeButton_.getProperties().set ("variant", juce::var ("primary"));
  silenceButton_.getProperties().set ("variant", juce::var ("secondary"));
  duplicateButton_.getProperties().set ("variant", juce::var ("secondary"));
  deleteButton_.getProperties().set ("variant", juce::var ("destructive"));
  deleteButton_.setColour (juce::TextButton::buttonColourId, juce::Colours::red);
  reverseButton_.getProperties().set ("variant", juce::var ("secondary"));
  previewButton_.getProperties().set ("variant", juce::var ("secondary"));
  previewButton_.setClickingTogglesState(true);
  exportButton_.getProperties().set ("variant", juce::var ("secondary"));

  undoButton_.setTooltip("Undo");
  redoButton_.setTooltip("Redo");
  rearrangeButton_.setTooltip("Randomize slice order");
  reverseButton_.setTooltip("Reverse selected slices");
  duplicateButton_.setTooltip("Duplicate selected slices");
  silenceButton_.setTooltip("Mute selected slices");
  deleteButton_.setTooltip("Delete selected slices");
  previewButton_.setTooltip("Preview playback");
  exportButton_.setTooltip("Export rearranged audio");

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

  const int bw = sliceshuffle::UiTokens::toolbarButtonWidth;
  const int bh = sliceshuffle::UiTokens::bottomBarRowHeight;
  const int rowGap = sliceshuffle::UiTokens::bottomBarRowGap;
  const int buttonSpacing = sliceshuffle::UiTokens::buttonSpacing;
  const int groupSpacing = sliceshuffle::UiTokens::groupSpacing;

  // Top row (under waveform): Shuffle, Reverse, Duplicate, Silence, Delete
  auto row1 = bottomBar.removeFromTop(bh);
  int x1 = row1.getX();
  const int y1 = row1.getY();
  const auto place1 = [&](juce::Component& c)
  {
    c.setBounds(x1, y1, bw, bh);
    x1 += bw + buttonSpacing;
  };
  place1(rearrangeButton_);
  place1(reverseButton_);
  place1(duplicateButton_);
  place1(silenceButton_);
  place1(deleteButton_);

  bottomBar.removeFromTop(rowGap);

  // Bottom row: Undo, Redo (narrow) | Preview, Export (right-justified)
  const int historyW = sliceshuffle::UiTokens::historyButtonWidth;
  auto row2 = bottomBar;
  const int y2 = row2.getY();
  undoButton_.setBounds(row2.getX(), y2, historyW, bh);
  redoButton_.setBounds(row2.getX() + historyW + buttonSpacing, y2, historyW, bh);
  int xRight = row2.getRight();
  exportButton_.setBounds(xRight - bw, y2, bw, bh);
  xRight -= bw + buttonSpacing;
  previewButton_.setBounds(xRight - bw, y2, bw, bh);

  waveformView_.setBounds(r);
}

void SliceShufflePluginEditor::timerCallback()
{
  topBar_.refresh();
  waveformOverview_.ensureEnvelopeBuilt();
  waveformOverview_.repaint();
  waveformView_.refresh();
  previewButton_.setButtonText(processorRef.isPreviewActive() ? "Stop" : "Preview");
  previewButton_.setToggleState(processorRef.isPreviewActive(), juce::dontSendNotification);
  undoButton_.setEnabled(processorRef.canUndo());
  redoButton_.setEnabled(processorRef.canRedo());

  const bool hasSelection = !waveformView_.getSelectedSliceIndices().empty();
  silenceButton_.setEnabled(hasSelection);
  deleteButton_.setEnabled(hasSelection);
  duplicateButton_.setEnabled(hasSelection);
  reverseButton_.setEnabled(hasSelection);

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
