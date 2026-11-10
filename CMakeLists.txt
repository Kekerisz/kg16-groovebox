cmake_minimum_required(VERSION 3.22)
project(KG16 VERSION 1.0.2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

option(KG16_TEST_ONLY "Build only the offline engine test (no plugin)" OFF)

# JUCE: use a local checkout if given (-DJUCE_DIR=...), otherwise download it.
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

if(KG16_TEST_ONLY)
    juce_add_console_app(RenderTest PRODUCT_NAME "RenderTest")
    target_sources(RenderTest PRIVATE tests/RenderTest.cpp)
    target_compile_definitions(RenderTest PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(RenderTest PRIVATE juce::juce_audio_formats juce::juce_recommended_config_flags)
    return()
endif()

if(APPLE)
    set(KG16_FORMATS AU VST3 Standalone)
else()
    set(KG16_FORMATS VST3 Standalone)
endif()

juce_add_plugin(KG16
    COMPANY_NAME "Kevin G Production"
    COMPANY_WEBSITE "https://kevingal.media"
    BUNDLE_ID "media.kevingal.kg16"
    PLUGIN_MANUFACTURER_CODE KGPr
    PLUGIN_CODE Kg16
    PRODUCT_NAME "KG-16 Groovebox"
    DESCRIPTION "Drum machine and bass groovebox"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Drum Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS ${KG16_FORMATS})

juce_add_binary_data(KG16Data SOURCES ui/index.html)

target_sources(KG16 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(KG16 PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(KG16
    PRIVATE
        KG16Data
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
