#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "PreparedState.h"

class WaveformView : public juce::Component, public juce::ChangeListener
{
public:
  explicit WaveformView(SlicerPluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void resized() override;
  void changeListenerCallback(juce::ChangeBroadcaster* source) override;

  /** Call when sample or slice map may have changed (e.g. from timer). */
  void refresh();

private:
  SlicerPluginProcessor& processor_;
  juce::AudioFormatManager formatManager_;
  juce::AudioThumbnailCache thumbnailCache_{1};
  juce::AudioThumbnail thumbnail_{512, formatManager_, thumbnailCache_};
  juce::File currentFile_;
};
