cmake_minimum_required(VERSION 3.22)
project(Stratum VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

option(STRATUM_BUILD_TESTS "Build the JUCE-free DSP self-test" ON)
option(STRATUM_BUILD_PLUGIN "Build the VST3 / Standalone plugin (needs JUCE)" ON)
option(STRATUM_COPY_AFTER_BUILD "Copy the VST3 into the system plug-in folder after building" OFF)
set(STRATUM_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of downloading one")

# ------------------------------------------------------------------ DSP self-test
if(STRATUM_BUILD_TESTS)
    add_executable(StratumDspTests Tests/dsp_test.cpp)
    target_include_directories(StratumDspTests PRIVATE Source/Dsp)
    if(NOT MSVC)
        target_compile_options(StratumDspTests PRIVATE -O2)
    endif()
    enable_testing()
    add_test(NAME stratum_dsp COMMAND StratumDspTests)
endif()

# ------------------------------------------------------------------ Plugin
if(STRATUM_BUILD_PLUGIN)
    if(STRATUM_JUCE_DIR)
        add_subdirectory(${STRATUM_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.8
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(Stratum
        COMPANY_NAME "Stratum Audio"
        PLUGIN_MANUFACTURER_CODE Strt
        PLUGIN_CODE Sta1
        FORMATS VST3 Standalone
        PRODUCT_NAME "Stratum"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD ${STRATUM_COPY_AFTER_BUILD})

    juce_generate_juce_header(Stratum)

    target_sources(Stratum PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_include_directories(Stratum PRIVATE Source)

    target_compile_definitions(Stratum PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0)

    target_link_libraries(Stratum
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
