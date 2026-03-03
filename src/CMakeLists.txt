# SliceShuffle targets: DSP library (JUCE-free), CLI app, and plugin.
# Build from repo root: cmake -B build && cmake --build build

# -----------------------------------------------------------------------------
# 1. DSP library — pure slicing/rearrange logic, no JUCE (testable without DAW)
# -----------------------------------------------------------------------------
add_library(sliceshuffle_dsp STATIC
  dsp/SliceShuffleEngine.cpp
)
target_include_directories(sliceshuffle_dsp PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/dsp"
)
target_compile_features(sliceshuffle_dsp PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# 2. Console app — load WAV → slice by BPM → shuffle → write WAV (offline)
# -----------------------------------------------------------------------------
juce_add_console_app(SliceShuffleCli
  PRODUCT_NAME "SliceShuffle CLI"
)

juce_generate_juce_header(SliceShuffleCli)

target_include_directories(SliceShuffleCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cli")

target_sources(SliceShuffleCli
  PRIVATE
  cli/Main.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
  cli/Renderer.cpp
)

target_compile_definitions(SliceShuffleCli
  PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(SliceShuffleCli
  PRIVATE
  sliceshuffle_dsp
  juce::juce_core
  juce::juce_events
  juce::juce_audio_formats
  juce::juce_audio_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags
)

# -----------------------------------------------------------------------------
# 3. Plugin — VST3 (+ Standalone) for real-time use in FL Studio etc.
# -----------------------------------------------------------------------------
juce_add_plugin(SliceShufflePlugin
  COMPANY_NAME "SliceShuffle"
  PLUGIN_MANUFACTURER_CODE Slsh
  PLUGIN_CODE SlS0
  FORMATS VST3 Standalone
  PRODUCT_NAME "SliceShuffle"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(SliceShufflePlugin)

target_include_directories(SliceShufflePlugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/plugin"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli"
)

target_sources(SliceShufflePlugin
  PRIVATE
  plugin/PluginProcessor.cpp
  plugin/PluginEditor.cpp
  plugin/SliceShuffleLookAndFeel.cpp
  plugin/StandaloneWindow.cpp
  plugin/TopBarComponent.cpp
  plugin/WaveformOverview.cpp
  plugin/WaveformView.cpp
  plugin/ControlPanel.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
)

target_compile_definitions(SliceShufflePlugin
  PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  PRIVATE
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_link_libraries(SliceShufflePlugin
  PRIVATE
  sliceshuffle_dsp
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_gui_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
