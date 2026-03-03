#pragma once

#include <JuceHeader.h>
#include <unordered_set>
#include <vector>
#include "PluginProcessor.h"
#include "PreparedState.h"

class WaveformView : public juce::Component, public juce::ChangeListener
{
public:
  explicit WaveformView(SliceShufflePluginProcessor& proc);

  void paint(juce::Graphics&) override;
  void resized() override;
  void mouseDown(const juce::MouseEvent& e) override;
  void mouseDrag(const juce::MouseEvent& e) override;
  void mouseUp(const juce::MouseEvent& e) override;
  bool keyPressed(const juce::KeyPress& key) override;
  void changeListenerCallback(juce::ChangeBroadcaster* source) override;

  /** Call when sample or slice map may have changed (e.g. from timer). */
  void refresh();

  /** Set the window range to display (used during overview drag for live sync). Pass start < 0 to clear. */
  void setDisplayWindowOverride(juce::int64 startSample, juce::int64 endSample);

  /** Returns the set of selected slice indices (shift+click). */
  const std::unordered_set<size_t>& getSelectedSliceIndices() const { return selectedSliceIndices_; }
  /** Set selection (e.g. after undo/redo so highlight follows the slice). */
  void setSelectedSliceIndices(std::unordered_set<size_t> indices)
  {
    selectedSliceIndices_ = std::move(indices);
    repaint();
  }

  /** Clear any slice selection (used on reset). */
  void clearSelection() { selectedSliceIndices_.clear(); repaint(); }

private:
  /** Given mouse x in component coords, return slice index under cursor or -1. */
  int sliceIndexAt(float x) const;
  /** All slice indices that overlap the horizontal range [x0, x1] (component coords). */
  std::unordered_set<size_t> sliceIndicesInXRange(float x0, float x1) const;
  /** Segment in the displayed window: (logical index, start offset in window buffer, length in samples). */
  struct WindowSegment { size_t logicalIndex; size_t startOffset; size_t length; };
  /** Build segments for the current display range (playback order). Used for hit-test and selection draw. */
  std::vector<WindowSegment> buildWindowSegments(const PreparedState& state,
                                                juce::int64 rangeStart,
                                                juce::int64 rangeEnd) const;

  static constexpr int kDragStartThresholdPx = 5;

  SliceShufflePluginProcessor& processor_;
  juce::Point<int> dragStartPos_;
  bool dragStarted_{false};
  bool shiftDragActive_{false};
  std::unordered_set<size_t> selectedSliceIndices_;
  /** When valid (overrideStart_ >= 0), bottom view shows this range (raw waveform) for live overview drag. */
  juce::int64 overrideStart_{-1};
  juce::int64 overrideEnd_{-1};
  juce::AudioFormatManager formatManager_;
  juce::AudioThumbnailCache thumbnailCache_{1};
  juce::AudioThumbnail thumbnail_{512, formatManager_, thumbnailCache_};
  juce::File currentFile_;
};
