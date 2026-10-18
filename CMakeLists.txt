cmake_minimum_required(VERSION 3.22)

project(GoodReverb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(GOOD_REVERB_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(GOOD_REVERB_BUILD_TESTS "Build the headless DSP tests" ON)

# ---------------------------------------------------------------------------
# DSP core (no JUCE dependency)
# ---------------------------------------------------------------------------
add_library(good_reverb_dsp STATIC
    Source/dsp/ReverbEngine.cpp
    Source/dsp/ReverbEngine.h
    Source/dsp/DspPrimitives.h)
target_include_directories(good_reverb_dsp PUBLIC Source)
if(MSVC)
    target_compile_options(good_reverb_dsp PRIVATE /W4)
else()
    target_compile_options(good_reverb_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(GOOD_REVERB_BUILD_TESTS)
    enable_testing()
    add_executable(GoodReverbTests tests/EngineTests.cpp)
    target_link_libraries(GoodReverbTests PRIVATE good_reverb_dsp)
    add_test(NAME GoodReverbTests COMMAND GoodReverbTests)
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(GOOD_REVERB_BUILD_PLUGIN)
    # Point JUCE_DIR at a local JUCE checkout to skip the download.
    if(DEFINED JUCE_DIR)
        add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    set(GOOD_REVERB_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND GOOD_REVERB_FORMATS AU)
    endif()
    if(UNIX AND NOT APPLE)
        list(APPEND GOOD_REVERB_FORMATS LV2)
    endif()

    juce_add_plugin(GoodReverb
        COMPANY_NAME "Good Reverb"
        BUNDLE_ID com.goodreverb.goodreverb
        PLUGIN_MANUFACTURER_CODE Gdrv
        PLUGIN_CODE Gdrb
        FORMATS ${GOOD_REVERB_FORMATS}
        PRODUCT_NAME "Good Reverb"
        DESCRIPTION "Algorithmic stereo reverb"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Reverb
        AU_MAIN_TYPE kAudioUnitType_Effect
        LV2URI https://github.com/bobanes1/good-reverb)

    target_sources(GoodReverb PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/GoodLookAndFeel.cpp)

    target_compile_definitions(GoodReverb PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(GoodReverb
        PRIVATE
            good_reverb_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
