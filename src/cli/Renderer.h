#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "SlicerEngine.h"
#include <vector>

/**
 * Offline render: copy slices from source to output in the given order,
 * with short crossfades at slice boundaries to avoid clicks.
 * Output length = sum of slice lengths (same as input length when just reordering).
 */
void renderSliced(juce::AudioBuffer<float>& out,
                  const juce::AudioBuffer<float>& source,
                  const std::vector<slicer::Slice>& slices,
                  const std::vector<size_t>& order,
                  double sampleRate,
                  double crossfadeMs = 5.0);
