# CutShuffle — BPM-based WAV slicer (console app → VST3)
# CMake-first, JUCE 7.x. Out-of-source build: cmake -B build && cmake --build build

cmake_minimum_required(VERSION 3.22)

project(CutShuffle VERSION 0.1.0
  LANGUAGES C CXX
  DESCRIPTION "BPM-slice WAV and rearrange slices randomly (CLI + plugin)")

# JUCE: use submodule at known commit (recommended)
# Clone with: git clone --recurse-submodules ... or after clone: git submodule update --init --recursive
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/juce/CMakeLists.txt")
  message(FATAL_ERROR
    "JUCE not found. Add it as a submodule:\n"
    "  git submodule add https://github.com/juce-framework/JUCE.git juce\n"
    "  git submodule update --init --recursive")
endif()
add_subdirectory(juce)

# Optional: use system JUCE instead of submodule
# find_package(JUCE CONFIG REQUIRED)

# All application and plugin targets live under src/
add_subdirectory(src)
