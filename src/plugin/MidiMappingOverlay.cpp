#include "MidiMappingOverlay.h"

namespace {
constexpr int kRootNote = 36; // C1 = slice 0
juce::String midiNoteToName(int note)
{
  static const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",  "F#", "G",  "G#", "A",  "A#", "B"};
  const int octave = note / 12 - 2;
  const int idx = note % 12;
  return juce::String(names[idx]) + juce::String(octave);
}
} // namespace

MidiMappingOverlay::MidiMappingOverlay(SlicerPluginProcessor& proc) : processor_(proc) {}

void MidiMappingOverlay::paint(juce::Graphics& g)
{
  g.fillAll(juce::Colours::transparentBlack);
  g.setColour(juce::Colours::white.withAlpha(0.9f));
  g.setFont(12.0f);

  const int numSlices = processor_.getNumSlices();
  const int lastSlice = processor_.lastTriggeredSliceIndex.load();

  juce::String text = "Slices: 0.." + juce::String(numSlices - 1);
  if (numSlices > 0)
    text += "  |  " + midiNoteToName(kRootNote) + " (36) = slice 0";
  if (numSlices > 0 && kRootNote + numSlices - 1 <= 127)
    text += "  |  Max note: " + midiNoteToName(kRootNote + numSlices - 1);
  if (lastSlice >= 0)
    text += "  |  Last: slice " + juce::String(lastSlice);

  g.drawText(text, getLocalBounds().reduced(4), juce::Justification::centredLeft, true);
}

void MidiMappingOverlay::refresh() { repaint(); }
