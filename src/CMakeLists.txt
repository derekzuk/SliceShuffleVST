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

target_sources(SlicerCli
  PRIVATE
  cli/Main.cpp
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
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
)

juce_generate_juce_header(SlicerPlugin)

target_include_directories(SlicerPlugin PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/plugin")

target_sources(SlicerPlugin
  PRIVATE
  plugin/PluginProcessor.cpp
  plugin/PluginEditor.cpp
)

target_compile_definitions(SlicerPlugin
  PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(SlicerPlugin
  PRIVATE
  slicer_dsp
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_lto_flags
  juce::juce_recommended_warning_flags
)
