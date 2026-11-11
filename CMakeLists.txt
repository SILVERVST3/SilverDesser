cmake_minimum_required(VERSION 3.22)

project(SilverDesser VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE e baixado automaticamente (mesma versao usada nos testes)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SilverDesser
    COMPANY_NAME "SILVER"
    PLUGIN_MANUFACTURER_CODE Slvr
    PLUGIN_CODE Sdes
    FORMATS VST3
    PRODUCT_NAME "SilverDesser"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics)

target_sources(SilverDesser PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/DesserDSP.h
    Source/Components.h
    Source/SpectrumDisplay.h
    Source/SilverTheme.h)

target_compile_definitions(SilverDesser PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SilverDesser
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
