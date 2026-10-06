cmake_minimum_required(VERSION 3.22)
project(SKIPFIEND VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE  -  fetched automatically. Set SKIPFIEND_JUCE_PATH to use a local copy.
# ---------------------------------------------------------------------------
set(SKIPFIEND_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(SKIPFIEND_JUCE_PATH)
    add_subdirectory(${SKIPFIEND_JUCE_PATH} juce_build)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Brand / support details. These are the ONLY place these strings live: they
# feed the VST3 module info a host displays, the About box, the in-plugin
# manual and the troubleshooting export. Replace these three lines before
# release and every one of those follows.
# ---------------------------------------------------------------------------
set(SKIPFIEND_HOMEPAGE        "https://djshellshoxxx.github.io/circuitdriftlabs/" CACHE STRING "Product homepage")
set(SKIPFIEND_SUPPORT_CONTACT "https://github.com/djshellshoxxx/skipfiend/issues" CACHE STRING "Support contact")
set(SKIPFIEND_GITHUB          "https://github.com/djshellshoxxx/skipfiend" CACHE STRING "Source repository")
option(SKIPFIEND_COPY_PLUGIN_AFTER_BUILD "Install built plug-in after a local build" ON)

juce_add_plugin(SKIPFIEND
    COMPANY_NAME              "Circuit Drift Labs"
    COMPANY_WEBSITE           "${SKIPFIEND_HOMEPAGE}"
    PLUGIN_MANUFACTURER_CODE  Fnda
    PLUGIN_CODE               Skpf
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "SKIPFIEND"
    VERSION                   ${PROJECT_VERSION}
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD   ${SKIPFIEND_COPY_PLUGIN_AFTER_BUILD}
    VST3_CATEGORIES           Fx Distortion
    ICON_BIG                  "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon.png"
    ICON_SMALL                "${CMAKE_CURRENT_SOURCE_DIR}/Source/icon_small.png"
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE)

juce_generate_juce_header(SKIPFIEND)

target_sources(SKIPFIEND PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SKIPFIEND PUBLIC
    SKIPFIEND_VERSION="${PROJECT_VERSION}"
    SKIPFIEND_HOMEPAGE="${SKIPFIEND_HOMEPAGE}"
    SKIPFIEND_SUPPORT_CONTACT="${SKIPFIEND_SUPPORT_CONTACT}"
    SKIPFIEND_GITHUB="${SKIPFIEND_GITHUB}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(SKIPFIEND
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# LTO's whole-program codegen link step is very RAM-hungry (multiple GB) and
# this project is built on memory-constrained machines; skip it rather than
# risk the linker getting OOM-killed. Re-enable juce_recommended_lto_flags
# above if building on a machine with plenty of headroom.

# ---------------------------------------------------------------------------
# Headless test harness. Off by default; it is a development tool, not part of
# the shipped plugin. Configure with -DSKIPFIEND_BUILD_TESTS=ON to get it.
# ---------------------------------------------------------------------------
option(SKIPFIEND_BUILD_TESTS "Build the headless test harness" OFF)

if(SKIPFIEND_BUILD_TESTS)
    juce_add_console_app(SKIPFIEND_Tests PRODUCT_NAME "SKIPFIEND_Tests")

    target_sources(SKIPFIEND_Tests PRIVATE Tests/HeadlessTests.cpp)

    target_compile_definitions(SKIPFIEND_Tests PRIVATE
        SKIPFIEND_VERSION="${PROJECT_VERSION}"
        SKIPFIEND_HOMEPAGE="${SKIPFIEND_HOMEPAGE}"
        SKIPFIEND_SUPPORT_CONTACT="${SKIPFIEND_SUPPORT_CONTACT}"
        SKIPFIEND_GITHUB="${SKIPFIEND_GITHUB}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        # test-only: lets the harness pump the message loop so juce::Timer
        # callbacks (the editor's meter/LED/feed updates) actually fire.
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(SKIPFIEND_Tests
        PRIVATE
            SKIPFIEND
            juce::juce_gui_basics
            juce::juce_gui_extra
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    # so the suite can be run with ctest as well as directly
    enable_testing()
    add_test(NAME skipfiend_headless COMMAND SKIPFIEND_Tests)
endif()
