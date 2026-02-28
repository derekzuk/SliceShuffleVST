#pragma once

#include <JuceHeader.h>
#include <unordered_set>
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
  bool keyPressed(const juce::KeyPress& key) override;
  void changeListenerCallback(juce::ChangeBroadcaster* source) override;

  /** Call when sample or slice map may have changed (e.g. from timer). */
  void refresh();

  /** Returns the set of selected slice indices (control+click). */
  const std::unordered_set<size_t>& getSelectedSliceIndices() const { return selectedSliceIndices_; }

  /** Clear any slice selection (used on reset). */
  void clearSelection() { selectedSliceIndices_.clear(); repaint(); }

private:
  /** Given mouse x in component coords, return slice index under cursor or -1. */
  int sliceIndexAt(float x) const;

  static constexpr int kDragStartThresholdPx = 5;

  SlicerPluginProcessor& processor_;
  juce::Point<int> dragStartPos_;
  bool dragStarted_{false};
  std::unordered_set<size_t> selectedSliceIndices_;
  juce::AudioFormatManager formatManager_;
  juce::AudioThumbnailCache thumbnailCache_{1};
  juce::AudioThumbnail thumbnail_{512, formatManager_, thumbnailCache_};
  juce::File currentFile_;
};
