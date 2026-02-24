#include "PluginProcessor.h"
#include "PluginEditor.h"

SlicerPluginEditor::SlicerPluginEditor(SlicerPluginProcessor& p)
  : AudioProcessorEditor(&p), processorRef(p)
{
  juce::ignoreUnused(processorRef);
  setSize(400, 200);
}

SlicerPluginEditor::~SlicerPluginEditor() = default;

void SlicerPluginEditor::paint(juce::Graphics& g)
{
  g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
  g.setColour(juce::Colours::white);
  g.setFont(16.0f);
  g.drawText("Slicer — BPM slice & shuffle (plugin stub)", getLocalBounds(),
             juce::Justification::centred, true);
}

void SlicerPluginEditor::resized() {}
