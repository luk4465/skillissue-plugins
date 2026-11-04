cmake_minimum_required(VERSION 3.22)

# Universal binary on macOS (Apple Silicon + Intel), set before project()
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

project(SkillIssue VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    # Static runtime: the .vst3 runs on machines without the VC++ redistributable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# JUCE: local copy in libs/JUCE, otherwise fetched (CI)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/JUCE/CMakeLists.txt")
    add_subdirectory(libs/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(SI_FORMATS VST3)
if(APPLE)
    list(APPEND SI_FORMATS AU)
endif()

juce_add_binary_data(SIAssets SOURCES
    resources/fonts/PlusJakartaSans-Regular.ttf
    resources/fonts/PlusJakartaSans-Medium.ttf
    resources/fonts/PlusJakartaSans-SemiBold.ttf
    resources/fonts/PlusJakartaSans-Bold.ttf
    resources/fonts/PlusJakartaSans-ExtraBold.ttf)

set(SI_SHARED_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/SIStyle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/SIWidgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/dsp/ReverbEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/dsp/FetComp.cpp)

# si_add_plugin(<target> <product name> <4-char code> <bundle suffix> <vst3 category> sources...)
function(si_add_plugin target product code bundle category)
    juce_add_plugin(${target}
        COMPANY_NAME "SkillIssue"
        COMPANY_WEBSITE "https://skillissue.audio"
        BUNDLE_ID com.skillissue.${bundle}
        PLUGIN_MANUFACTURER_CODE Siau
        PLUGIN_CODE ${code}
        FORMATS ${SI_FORMATS}
        PRODUCT_NAME "${product}"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx ${category}
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(${target} PRIVATE ${ARGN} ${SI_SHARED_SOURCES})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shared)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(${target}
        PRIVATE
            SIAssets
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

si_add_plugin(SIReverb "SI Reverb" Sirv sireverb Reverb
    plugins/SIReverb/PluginProcessor.cpp
    plugins/SIReverb/PluginEditor.cpp)

si_add_plugin(SIRiser "SI Riser" Sirs siriser Reverb
    plugins/SIRiser/PluginProcessor.cpp
    plugins/SIRiser/PluginEditor.cpp
    plugins/SIRiser/RiserRenderer.cpp)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SIEQ/PluginEditor.cpp")
    si_add_plugin(SIEQ "SI EQ" Sieq sieq EQ
        plugins/SIEQ/PluginProcessor.cpp
        plugins/SIEQ/PluginEditor.cpp
        plugins/SIEQ/EqEngine.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SI76/PluginEditor.cpp")
    si_add_plugin(SI76 "SI 76" Si76 si76 Dynamics
        plugins/SI76/PluginProcessor.cpp
        plugins/SI76/PluginEditor.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SISaturn/PluginEditor.cpp")
    si_add_plugin(SISaturn "SI Saturn" Sist sisaturn Distortion
        plugins/SISaturn/PluginProcessor.cpp
        plugins/SISaturn/PluginEditor.cpp
        plugins/SISaturn/SaturnEngine.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SIEcho/PluginEditor.cpp")
    si_add_plugin(SIEcho "SI Echo" Siec siecho Delay
        plugins/SIEcho/PluginProcessor.cpp
        plugins/SIEcho/PluginEditor.cpp
        plugins/SIEcho/EchoEngine.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SIRack/PluginEditor.cpp")
    si_add_plugin(SIRack "SI Rack" Sirk sirack Dynamics
        plugins/SIRack/PluginProcessor.cpp
        plugins/SIRack/PluginEditor.cpp
        plugins/SIRack/Modules.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SITune/PluginEditor.cpp")
    si_add_plugin(SITune "SI Tune" Situ situne "Pitch Shift"
        plugins/SITune/PluginProcessor.cpp
        plugins/SITune/PluginEditor.cpp
        plugins/SITune/TuneEngine.cpp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SIPitch/PluginEditor.cpp")
    si_add_plugin(SIPitch "SI Pitch" Sipt sipitch "Pitch Shift"
        plugins/SIPitch/PluginProcessor.cpp
        plugins/SIPitch/PluginEditor.cpp
        plugins/SIPitch/PitchEngine.cpp)
endif()

# ------------------------------------------------------------------- tests
option(SI_BUILD_TESTS "Build the SITests runner (DSP + VST3 host checks)" ON)
if(SI_BUILD_TESTS)
    juce_add_console_app(SITests PRODUCT_NAME "SITests")
    target_sources(SITests PRIVATE
        tests/SITests.cpp
        tests/PluginMeasure.cpp
        shared/dsp/ReverbEngine.cpp
        plugins/SIRiser/RiserRenderer.cpp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SIEQ/EqEngine.cpp")
        target_sources(SITests PRIVATE plugins/SIEQ/EqEngine.cpp)
        target_compile_definitions(SITests PRIVATE SI_TEST_EQ=1)
    endif()
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SISaturn/SaturnEngine.cpp")
        target_sources(SITests PRIVATE plugins/SISaturn/SaturnEngine.cpp)
        target_compile_definitions(SITests PRIVATE SI_TEST_SAT=1)
    endif()
    target_sources(SITests PRIVATE plugins/SIEcho/EchoEngine.cpp plugins/SIRack/Modules.cpp shared/dsp/FetComp.cpp)
    target_compile_definitions(SITests PRIVATE SI_TEST_ECHO=1)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/plugins/SITune/TuneEngine.cpp")
        target_sources(SITests PRIVATE plugins/SITune/TuneEngine.cpp)
        target_compile_definitions(SITests PRIVATE SI_TEST_TUNE=1)
    endif()
    target_sources(SITests PRIVATE plugins/SIPitch/PitchEngine.cpp plugins/SIPitch/PluginProcessor.cpp plugins/SIPitch/PluginEditor.cpp
        shared/SIStyle.cpp shared/SIWidgets.cpp shared/PresetManager.cpp)
    target_link_libraries(SITests PRIVATE SIAssets)
    target_compile_definitions(SITests PRIVATE SI_TEST_PITCH=1)
    target_include_directories(SITests PRIVATE shared plugins/SIRiser plugins/SIEQ plugins/SITune plugins/SISaturn plugins/SIEcho plugins/SIRack plugins/SIPitch)
    target_compile_definitions(SITests PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(SITests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
