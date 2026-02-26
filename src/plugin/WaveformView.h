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
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void changeListenerCallback(juce::ChangeBroadcaster* source) override;

  /** Call when sample or slice map may have changed (e.g. from timer). */
  void refresh();

private:
  static constexpr int kDragStartThresholdPx = 5;

  SlicerPluginProcessor& processor_;
  juce::Point<int> dragStartPos_;
  bool dragStarted_{false};
  juce::AudioFormatManager formatManager_;
  juce::AudioThumbnailCache thumbnailCache_{1};
  juce::AudioThumbnail thumbnail_{512, formatManager_, thumbnailCache_};
  juce::File currentFile_;
};
