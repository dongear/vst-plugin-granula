cmake_minimum_required(VERSION 3.22)

project(Artempidor VERSION 0.6.0 LANGUAGES C CXX)

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
    cmake_parse_arguments(G "" "PRODUCT;CODE;SYNTH;AU_TYPE;DESC" "CATEGORIES;SOURCES" ${ARGN})
    juce_add_plugin(${target}
        COMPANY_NAME "artempidor"
        BUNDLE_ID "com.artempidor.${target}"
        PLUGIN_MANUFACTURER_CODE Artm
        PLUGIN_CODE ${G_CODE}
        FORMATS ${GRANULA_FORMATS}
        PRODUCT_NAME "${G_PRODUCT}"
        DESCRIPTION "${G_DESC}"
        IS_SYNTH ${G_SYNTH}
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES ${G_CATEGORIES}
        AU_MAIN_TYPE ${G_AU_TYPE})

    target_sources(${target} PRIVATE ${G_SOURCES})

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
            juce::juce_dsp
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
    CATEGORIES Instrument Sampler
    SOURCES Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/Parameters.cpp)

# Interference: harmonic "sweetener" effect (snaps audio to a key).
granula_add_plugin(Interference
    PRODUCT "Interference" CODE Itf1 SYNTH FALSE AU_TYPE kAudioUnitType_MusicEffect
    DESC "Harmonic sweetener: pulls any sound into key"
    CATEGORIES Fx EQ Filter
    SOURCES interference/Source/InterferenceProcessor.cpp interference/Source/InterferenceEditor.cpp)

# STONE: modal percussion synth with trails.
granula_add_plugin(Stone
    PRODUCT "STONE" CODE Stn1 SYNTH TRUE AU_TYPE kAudioUnitType_MusicDevice
    DESC "Organic modal percussion: stone, glass, wood, metal, water"
    CATEGORIES Instrument Drum
    SOURCES stone/Source/StoneProcessor.cpp stone/Source/StoneEditor.cpp)

# GARDEN: organic playable synth grown from a seeded plant.
granula_add_plugin(Garden
    PRODUCT "GARDEN" CODE Gdn1 SYNTH TRUE AU_TYPE kAudioUnitType_MusicDevice
    DESC "Organic synth: grow a plant, play its sound"
    CATEGORIES Instrument Synth
    SOURCES garden/Source/GardenProcessor.cpp garden/Source/GardenEditor.cpp)

# ---------------------------------------------------------------------------
# Offline test: renders audio through the DSP chain (no GUI, no host needed).
# ---------------------------------------------------------------------------
option(GRANULA_BUILD_TESTS "Build the offline DSP render test" OFF)
if(GRANULA_BUILD_TESTS)
    add_executable(garden_test tests/garden_test.cpp)
    target_include_directories(garden_test PRIVATE Source)
    add_executable(stone_test tests/stone_test.cpp)
    target_include_directories(stone_test PRIVATE Source)
    add_executable(granula_render tests/render.cpp)
    target_include_directories(granula_render PRIVATE Source)

    juce_add_console_app(interference_test PRODUCT_NAME interference_test)
    target_sources(interference_test PRIVATE tests/interference_test.cpp)
    target_compile_definitions(interference_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(interference_test PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)

    juce_add_console_app(tune_demo PRODUCT_NAME tune_demo)
    target_sources(tune_demo PRIVATE tests/tune_demo.cpp)
    target_compile_definitions(tune_demo PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(tune_demo PRIVATE juce::juce_dsp juce::juce_recommended_config_flags)
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
    add_executable(Interference_snapshot tests/interference_snapshot.cpp)
    target_include_directories(Interference_snapshot PRIVATE interference/Source)
    target_link_libraries(Interference_snapshot PRIVATE Interference juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics)
    target_compile_definitions(Interference_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Interference,INTERFACE_COMPILE_DEFINITIONS>)
    add_executable(interference_bypass_test tests/interference_bypass_test.cpp)
    target_include_directories(interference_bypass_test PRIVATE interference/Source)
    target_link_libraries(interference_bypass_test PRIVATE Interference juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics)
    target_compile_definitions(interference_bypass_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Interference,INTERFACE_COMPILE_DEFINITIONS>)
    add_executable(Stone_snapshot tests/stone_snapshot.cpp)
    target_include_directories(Stone_snapshot PRIVATE stone/Source)
    target_link_libraries(Stone_snapshot PRIVATE Stone juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics)
    target_compile_definitions(Stone_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Stone,INTERFACE_COMPILE_DEFINITIONS>)
    add_executable(Garden_snapshot tests/garden_snapshot.cpp)
    target_include_directories(Garden_snapshot PRIVATE garden/Source)
    target_link_libraries(Garden_snapshot PRIVATE Garden juce::juce_audio_processors juce::juce_dsp juce::juce_gui_basics)
    target_compile_definitions(Garden_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Garden,INTERFACE_COMPILE_DEFINITIONS>)
    add_executable(preset_test tests/preset_test.cpp)
    target_include_directories(preset_test PRIVATE Source)
    target_link_libraries(preset_test PRIVATE Artempidor juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics)
    target_compile_definitions(preset_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<TARGET_PROPERTY:Artempidor,INTERFACE_COMPILE_DEFINITIONS>)
endif()
