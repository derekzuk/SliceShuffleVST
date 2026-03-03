# CutShuffle targets: DSP library (JUCE-free), CLI app, and plugin.
# Build from repo root: cmake -B build && cmake --build build

# -----------------------------------------------------------------------------
# 1. DSP library — pure slicing/rearrange logic, no JUCE (testable without DAW)
# -----------------------------------------------------------------------------
add_library(cutshuffle_dsp STATIC
  dsp/CutShuffleEngine.cpp
)
target_include_directories(cutshuffle_dsp PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/dsp"
)
target_compile_features(cutshuffle_dsp PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# 2. Console app — load WAV → slice by BPM → shuffle → write WAV (offline)
# -----------------------------------------------------------------------------
juce_add_console_app(CutShuffleCli
  PRODUCT_NAME "CutShuffle CLI"
)

juce_generate_juce_header(CutShuffleCli)

target_include_directories(CutShuffleCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cli")

target_sources(CutShuffleCli
  PRIVATE
  cli/Main.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
  cli/Renderer.cpp
)

target_compile_definitions(CutShuffleCli
  PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(CutShuffleCli
  PRIVATE
  cutshuffle_dsp
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
juce_add_plugin(CutShufflePlugin
  COMPANY_NAME "CutShuffle"
  PLUGIN_MANUFACTURER_CODE Ctsh
  PLUGIN_CODE CtS0
  FORMATS VST3 Standalone
  PRODUCT_NAME "CutShuffle"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(CutShufflePlugin)

target_include_directories(CutShufflePlugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/plugin"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli"
)

target_sources(CutShufflePlugin
  PRIVATE
  plugin/PluginProcessor.cpp
  plugin/PluginEditor.cpp
  plugin/CutShuffleLookAndFeel.cpp
  plugin/StandaloneWindow.cpp
  plugin/TopBarComponent.cpp
  plugin/WaveformOverview.cpp
  plugin/WaveformView.cpp
  plugin/ControlPanel.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
)

target_compile_definitions(CutShufflePlugin
  PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  PRIVATE
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_link_libraries(CutShufflePlugin
  PRIVATE
  cutshuffle_dsp
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_gui_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
