cmake_minimum_required(VERSION 3.22)

project(MonieMidiMimic VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # One self-contained .vst3 with no Visual C++ runtime DLL needed on the target machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(MIMIC_BUILD_PLUGIN "Build the plugin (VST3 + Standalone)" ON)
option(MIMIC_BUILD_TESTS  "Build the headless engine tests" OFF)
set(MIMIC_FORMATS "VST3;Standalone" CACHE STRING "Plugin formats to build")

# ---- JUCE ------------------------------------------------------------------
# Use a local checkout with -DJUCE_DIR=path/to/JUCE, otherwise it is downloaded.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/Engine/Phrase.h
    Source/Engine/MidiIO.h          Source/Engine/MidiIO.cpp
    Source/Engine/Generator.h       Source/Engine/Generator.cpp
    Source/Engine/AudioToMidi.h     Source/Engine/AudioToMidi.cpp)

# ---- Plugin ----------------------------------------------------------------
if(MIMIC_BUILD_PLUGIN)
    juce_add_plugin(MonieMidiMimic
        COMPANY_NAME                "Monie"
        BUNDLE_ID                   com.monie.midimimic
        PLUGIN_MANUFACTURER_CODE    Mnie
        PLUGIN_CODE                 MMim
        FORMATS                     ${MIMIC_FORMATS}
        PRODUCT_NAME                "Monie MIDI Mimic"
        IS_SYNTH                    TRUE
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES             Instrument Generator
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(MonieMidiMimic PRIVATE
        ${ENGINE_SOURCES}
        Source/PluginProcessor.h    Source/PluginProcessor.cpp
        Source/PluginEditor.h       Source/PluginEditor.cpp
        Source/PreviewSynth.h
        Source/PianoRoll.h
        Source/MimicLookAndFeel.h)

    target_compile_definitions(MonieMidiMimic PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if(MSVC)
        target_compile_options(MonieMidiMimic PRIVATE /utf-8 /bigobj)
    endif()

    target_link_libraries(MonieMidiMimic
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---- Host test: loads the built VST3, plays it, renders the UI to a PNG --------
option(MIMIC_BUILD_HOST_TEST "Build the VST3 host smoke test" OFF)
if(MIMIC_BUILD_HOST_TEST)
    juce_add_console_app(MimicHostTest PRODUCT_NAME "MimicHostTest")
    target_sources(MimicHostTest PRIVATE
        ${ENGINE_SOURCES}
        Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Tests/HostTest.cpp)
    target_compile_definitions(MimicHostTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MimicHostTest PRIVATE
        juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---- Engine tests (no GUI, no audio device) ---------------------------------
if(MIMIC_BUILD_TESTS)
    juce_add_console_app(MimicEngineTests PRODUCT_NAME "MimicEngineTests")
    target_sources(MimicEngineTests PRIVATE ${ENGINE_SOURCES} Tests/EngineTests.cpp)
    target_compile_definitions(MimicEngineTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(MimicEngineTests PRIVATE
        juce::juce_core juce::juce_audio_basics juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
