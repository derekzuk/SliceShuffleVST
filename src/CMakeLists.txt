# Slicer targets: DSP library (JUCE-free), CLI app, and plugin.
# Build from repo root: cmake -B build && cmake --build build

# -----------------------------------------------------------------------------
# 1. DSP library — pure slicing/rearrange logic, no JUCE (testable without DAW)
# -----------------------------------------------------------------------------
add_library(slicer_dsp STATIC
  dsp/SlicerEngine.cpp
)
target_include_directories(slicer_dsp PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/dsp"
)
target_compile_features(slicer_dsp PUBLIC cxx_std_17)

# -----------------------------------------------------------------------------
# 2. Console app — load WAV → slice by BPM → shuffle → write WAV (offline)
# -----------------------------------------------------------------------------
juce_add_console_app(SlicerCli
  PRODUCT_NAME "Slicer CLI"
)

juce_generate_juce_header(SlicerCli)

target_include_directories(SlicerCli PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/cli")

target_sources(SlicerCli
  PRIVATE
  cli/Main.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
  cli/Renderer.cpp
)

target_compile_definitions(SlicerCli
  PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
)

target_link_libraries(SlicerCli
  PRIVATE
  slicer_dsp
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
juce_add_plugin(SlicerPlugin
  COMPANY_NAME "Slicer"
  PLUGIN_MANUFACTURER_CODE Slcr
  PLUGIN_CODE Slc0
  FORMATS VST3 Standalone
  PRODUCT_NAME "Slicer"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

juce_generate_juce_header(SlicerPlugin)

target_include_directories(SlicerPlugin PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/plugin"
  "${CMAKE_CURRENT_SOURCE_DIR}/cli"
)

target_sources(SlicerPlugin
  PRIVATE
  plugin/PluginProcessor.cpp
  plugin/PluginEditor.cpp
  plugin/StandaloneWindow.cpp
  plugin/TopBarComponent.cpp
  plugin/WaveformOverview.cpp
  plugin/WaveformView.cpp
  plugin/ControlPanel.cpp
  cli/WavLoader.cpp
  cli/WavWriter.cpp
)

target_compile_definitions(SlicerPlugin
  PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  PRIVATE
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)

target_link_libraries(SlicerPlugin
  PRIVATE
  slicer_dsp
  juce::juce_audio_utils
  juce::juce_audio_formats
  juce::juce_gui_basics
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
