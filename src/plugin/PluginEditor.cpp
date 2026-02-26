#include "PluginEditor.h"
#include "../cli/WavWriter.h"

namespace {
const juce::Identifier kSlicerExportDragType("slicer-export");
constexpr const char* kBpmId = "bpm";
constexpr const char* kWindowBeatsId = "windowBeats";
constexpr const char* kWindowPositionId = "windowPosition";
} // namespace

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

bool SlicerPluginEditor::shouldDropFilesWhenDraggedExternally(
    const juce::DragAndDropTarget::SourceDetails& sourceDetails,
    juce::StringArray& files,
    bool& canMoveFiles)
{
  if (sourceDetails.description != juce::var(kSlicerExportDragType.toString()))
    return false;
  auto state = processorRef.getPreparedState();
  if (!state || state->buffer.getNumSamples() == 0 || state->sampleRate <= 0)
    return false;

  const juce::AudioBuffer<float>& fullBuffer = state->buffer;
  const juce::int64 totalSamples = fullBuffer.getNumSamples();
  const double bpm = static_cast<double>(processorRef.getValueTreeState().getRawParameterValue(kBpmId)->load());
  int startSample = 0;
  int lengthSamples = static_cast<int>(totalSamples);

  if (bpm > 0.0 && state->sampleRate > 0.0)
  {
    const double secondsPerBeat = 60.0 / bpm;
    const double totalBeats = static_cast<double>(totalSamples) / (state->sampleRate * secondsPerBeat);
    const int windowBeatsParam = juce::jlimit(
        2, 64,
        static_cast<int>(std::round(processorRef.getValueTreeState().getRawParameterValue(kWindowBeatsId)->load())));
    const double windowBeats = static_cast<double>(windowBeatsParam);
    const float posNorm = processorRef.getValueTreeState().getRawParameterValue(kWindowPositionId)->load();
    const double startBeat = totalBeats <= windowBeats ? 0.0 : static_cast<double>(posNorm) * std::max(0.0, totalBeats - windowBeats);
    startSample = static_cast<int>(std::round(startBeat * secondsPerBeat * state->sampleRate));
    const int endSample = static_cast<int>(juce::jmin(
        totalSamples,
        static_cast<juce::int64>(std::round((startBeat + windowBeats) * secondsPerBeat * state->sampleRate))));
    lengthSamples = juce::jmax(0, endSample - startSample);
    startSample = juce::jlimit(0, static_cast<int>(totalSamples), startSample);
    lengthSamples = juce::jmin(lengthSamples, static_cast<int>(totalSamples) - startSample);
  }

  if (lengthSamples <= 0)
    return false;
  juce::AudioBuffer<float> windowBuffer(fullBuffer.getNumChannels(), lengthSamples);
  for (int ch = 0; ch < fullBuffer.getNumChannels(); ++ch)
    windowBuffer.copyFrom(ch, 0, fullBuffer, ch, startSample, lengthSamples);

  juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
  juce::File outFile = tempDir.getChildFile("Slicer_export_" + juce::Uuid().toDashedString() + ".wav");
  if (!writeWav(outFile, windowBuffer, state->sampleRate))
    return false;
  files.add(outFile.getFullPathName());
  canMoveFiles = false;
  return true;
}
