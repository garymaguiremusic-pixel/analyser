cmake_minimum_required(VERSION 3.22)

project(Analyser VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------
# Options
option(ANALYSER_BUILD_STANDALONE "Also build a standalone app for testing"                     OFF)
option(ANALYSER_BUILD_SCREENSHOT "Build the GUI screenshot tool (renders docs/screenshot.png)"  OFF)
option(ANALYSER_COPY_AFTER_BUILD "Install the plugin into the user plugin folder after building" OFF)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (leave empty to download JUCE 8.0.4)")

# ------------------------------------------------------------------------------
# JUCE
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Plugin (VST3 only for now)
set(ANALYSER_FORMATS VST3)
if(ANALYSER_BUILD_STANDALONE)
    list(APPEND ANALYSER_FORMATS Standalone)
endif()

set(ANALYSER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/CustomLookAndFeel.cpp
    Source/UI/AnalyserComponents.cpp
    Source/DSP/BandMeter.cpp)

set(ANALYSER_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

juce_add_plugin(Analyser
    PRODUCT_NAME                "ANALYSER"
    COMPANY_NAME                "Gary Maguire Music"
    BUNDLE_ID                   com.garymaguiremusic.analyser
    PLUGIN_MANUFACTURER_CODE    Gmgm
    PLUGIN_CODE                 Anl1
    FORMATS                     ${ANALYSER_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Analyzer
    COPY_PLUGIN_AFTER_BUILD     ${ANALYSER_COPY_AFTER_BUILD})

target_sources(Analyser PRIVATE ${ANALYSER_SOURCES})
target_include_directories(Analyser PRIVATE Source)
target_compile_definitions(Analyser PUBLIC ${ANALYSER_DEFINITIONS})

target_link_libraries(Analyser
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Screenshot tool: feeds test audio through the processor and saves the editor as a PNG.
if(ANALYSER_BUILD_SCREENSHOT)
    juce_add_console_app(AnalyserScreenshot PRODUCT_NAME "AnalyserScreenshot")
    target_sources(AnalyserScreenshot PRIVATE Tools/Screenshot.cpp ${ANALYSER_SOURCES})
    target_include_directories(AnalyserScreenshot PRIVATE Source)
    target_compile_definitions(AnalyserScreenshot PRIVATE ${ANALYSER_DEFINITIONS} JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(AnalyserScreenshot
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
            juce::juce_recommended_config_flags)
endif()
