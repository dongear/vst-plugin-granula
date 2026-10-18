cmake_minimum_required(VERSION 3.22)

project(Artempidor VERSION 0.3.1 LANGUAGES C CXX)

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

function(granula_add_plugin target)
    cmake_parse_arguments(G "" "PRODUCT;CODE;SYNTH;AU_TYPE;DESC" "CATEGORIES" ${ARGN})
    juce_add_plugin(${target}
        COMPANY_NAME "artempidor"
        BUNDLE_ID "com.artempidor.${target}"
        PLUGIN_MANUFACTURER_CODE Artm
        PLUGIN_CODE ${G_CODE}
        FORMATS ${GRANULA_FORMATS}
        PRODUCT_NAME "${G_PRODUCT}"
        DESCRIPTION "${G_DESC}"
        IS_SYNTH ${G_SYNTH}
        NEEDS_MIDI_INPUT ${G_SYNTH}
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${G_CATEGORIES}
        AU_MAIN_TYPE ${G_AU_TYPE})

    target_sources(${target} PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Parameters.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_USE_WINDOWS_MEDIA_FORMAT=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    if("Standalone" IN_LIST GRANULA_FORMATS)
        target_link_libraries(${target} PRIVATE juce::juce_audio_utils)
    endif()

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# One plugin: instrument with an audio (sidechain) input, SAMPLE / INPUT modes.
granula_add_plugin(Artempidor
    PRODUCT "artempidor" CODE Atp1 SYNTH TRUE AU_TYPE kAudioUnitType_MusicDevice
    DESC "Granular sampler and live granulator with cassette, delay and reverb"
    CATEGORIES Instrument Sampler)

# ---------------------------------------------------------------------------
# Offline test: renders audio through the DSP chain (no GUI, no host needed).
# ---------------------------------------------------------------------------
option(GRANULA_BUILD_TESTS "Build the offline DSP render test" OFF)
if(GRANULA_BUILD_TESTS)
    add_executable(granula_render tests/render.cpp)
    target_include_directories(granula_render PRIVATE Source)
endif()

# Headless UI snapshots (render each editor to a PNG).
option(GRANULA_BUILD_SNAPSHOT "Build the editor snapshot tools" OFF)
if(GRANULA_BUILD_SNAPSHOT)
    foreach(t Artempidor)
        add_executable(${t}_snapshot tests/snapshot.cpp)
        target_include_directories(${t}_snapshot PRIVATE Source)
        target_link_libraries(${t}_snapshot PRIVATE ${t} juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics)
        target_compile_definitions(${t}_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            $<TARGET_PROPERTY:${t},INTERFACE_COMPILE_DEFINITIONS>)
    endforeach()
    add_executable(preset_test tests/preset_test.cpp)
    target_include_directories(preset_test PRIVATE Source)
    target_link_libraries(preset_test PRIVATE Artempidor juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics)
    target_compile_definitions(preset_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Artempidor,INTERFACE_COMPILE_DEFINITIONS>)
endif()
