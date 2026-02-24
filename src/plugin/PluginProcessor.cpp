#include "PluginProcessor.h"
#include "PluginEditor.h"

SlicerPluginProcessor::SlicerPluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
  : AudioProcessor(
      BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
#endif
{
}

SlicerPluginProcessor::~SlicerPluginProcessor() = default;

const juce::String SlicerPluginProcessor::getName() const { return JucePlugin_Name; }

bool SlicerPluginProcessor::acceptsMidi() const { return false; }
bool SlicerPluginProcessor::producesMidi() const { return false; }
bool SlicerPluginProcessor::isMidiEffect() const { return false; }
double SlicerPluginProcessor::getTailLengthSeconds() const { return 0.0; }

int SlicerPluginProcessor::getNumPrograms() { return 1; }
int SlicerPluginProcessor::getCurrentProgram() { return 0; }
void SlicerPluginProcessor::setCurrentProgram(int) {}
const juce::String SlicerPluginProcessor::getProgramName(int) { return {}; }
void SlicerPluginProcessor::changeProgramName(int, const juce::String&) {}

void SlicerPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
  juce::ignoreUnused(sampleRate, samplesPerBlock);
}

void SlicerPluginProcessor::releaseResources() {}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SlicerPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
  if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
      && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
    return false;
  if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
    return false;
  return true;
}
#endif

void SlicerPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
  juce::ignoreUnused(midiMessages);
  juce::ScopedNoDenormals noDenormals;
  // Placeholder: pass-through. Will use SlicerEngine + preloaded buffer later.
  for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    buffer.clear(ch, 0, buffer.getNumSamples());
}

bool SlicerPluginProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* SlicerPluginProcessor::createEditor() { return new SlicerPluginEditor(*this); }

void SlicerPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
  juce::ignoreUnused(destData);
}

void SlicerPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
  juce::ignoreUnused(data, sizeInBytes);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SlicerPluginProcessor(); }
