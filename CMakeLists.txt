cmake_minimum_required(VERSION 3.22)
project(NovaTune VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NOVATUNE_BUILD_PLUGIN "Build the JUCE plugin (VST3 / AU / Standalone)" ON)
option(NOVATUNE_BUILD_TESTS  "Build the offline DSP tests (no JUCE needed)"  OFF)

# ---------------------------------------------------------------------------
# Offline DSP tests (header-only engine, no dependencies)
# ---------------------------------------------------------------------------
if(NOVATUNE_BUILD_TESTS)
    enable_testing()
    add_executable(engine_test Tests/EngineTest.cpp)
    target_include_directories(engine_test PRIVATE Source)
    add_test(NAME engine COMMAND engine_test)
endif()

if(NOT NOVATUNE_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# JUCE (downloaded automatically the first time you configure)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

set(NOVATUNE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NOVATUNE_FORMATS AU)
endif()

juce_add_plugin(NovaTune
    COMPANY_NAME              "NovaAudio"
    PLUGIN_MANUFACTURER_CODE  Nova
    PLUGIN_CODE               Ntun
    FORMATS                   ${NOVATUNE_FORMATS}
    PRODUCT_NAME              "NovaTune"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    VST3_CATEGORIES           Fx Pitch\ Shift
    COPY_PLUGIN_AFTER_BUILD   FALSE)

juce_generate_juce_header(NovaTune)

target_sources(NovaTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DSP/AutoTuneEngine.h
    Source/DSP/PitchDetector.h
    Source/DSP/PitchShifter.h
    Source/DSP/Scales.h)

target_include_directories(NovaTune PRIVATE Source)

target_compile_definitions(NovaTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(NovaTune
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
