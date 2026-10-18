cmake_minimum_required(VERSION 3.22)

project(Granula VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if JUCE_PATH is given, otherwise download it.
# ---------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Formats to build. Override with -DGRANULA_FORMATS="VST3" for a quick build.
if(APPLE)
    set(GRANULA_DEFAULT_FORMATS VST3 AU Standalone)
else()
    set(GRANULA_DEFAULT_FORMATS VST3 Standalone)
endif()
set(GRANULA_FORMATS "${GRANULA_DEFAULT_FORMATS}" CACHE STRING "Plugin formats to build")

juce_add_plugin(Granula
    COMPANY_NAME "Granula Audio"
    BUNDLE_ID "com.granula-audio.granula"
    PLUGIN_MANUFACTURER_CODE Gnla
    PLUGIN_CODE Grn1
    FORMATS ${GRANULA_FORMATS}
    PRODUCT_NAME "Granula"
    DESCRIPTION "Granular texture machine with cassette, delay and reverb"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Delay Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Granula PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Parameters.cpp)

target_compile_definitions(Granula PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if("Standalone" IN_LIST GRANULA_FORMATS)
    target_link_libraries(Granula PRIVATE juce::juce_audio_utils)
endif()

target_link_libraries(Granula
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test: renders audio through the DSP chain (no GUI, no host needed).
# ---------------------------------------------------------------------------
option(GRANULA_BUILD_TESTS "Build the offline DSP render test" OFF)
if(GRANULA_BUILD_TESTS)
    add_executable(granula_render tests/render.cpp)
    target_include_directories(granula_render PRIVATE Source)
endif()

# Headless UI snapshot (renders the editor to a PNG).
option(GRANULA_BUILD_SNAPSHOT "Build the editor snapshot tool" OFF)
if(GRANULA_BUILD_SNAPSHOT)
    add_executable(granula_snapshot tests/snapshot.cpp)
    target_include_directories(granula_snapshot PRIVATE Source)
    target_link_libraries(granula_snapshot PRIVATE Granula juce::juce_audio_processors juce::juce_gui_basics)
    target_compile_definitions(granula_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Granula,INTERFACE_COMPILE_DEFINITIONS>)
endif()
