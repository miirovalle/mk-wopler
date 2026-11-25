cmake_minimum_required(VERSION 3.22)

project(MKWOPLER VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE builds its juceaide host tool by re-invoking CMake and forwards
# CMAKE_OSX_DEPLOYMENT_TARGET to that macOS configure. Passing an iOS value such
# as 17.0 therefore asks for a macOS binary for a macOS version that does not
# exist, and the tool build dies with "No CMAKE_C_COMPILER could be found".
# Only ever default the macOS deployment target here; iOS is configured through
# IPHONEOS_DEPLOYMENT_TARGET instead.
if(NOT IOS AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")
endif()

option(WOB_BUILD_PLUGIN "Build the AUv3/VST3 plugin targets" ON)
option(WOB_BUILD_TESTS "Build the offline DSP test harness" ON)

# Everything plugin related is optional so the DSP harness can be configured on
# a machine without JUCE, GUI libraries or audio development packages.
if(WOB_BUILD_PLUGIN)

# ---------------------------------------------------------------------------
# JUCE
#
# Point JUCE_PATH at an existing checkout, or let FetchContent grab it.
# FetchContent_MakeAvailable() already performs its own add_subdirectory(), so
# the two branches must not both add JUCE or CMake rejects the duplicate binary
# directory. CI has no External/JUCE (it is gitignored), so it takes the
# FetchContent branch.
# ---------------------------------------------------------------------------
if(NOT DEFINED JUCE_PATH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/External/JUCE/CMakeLists.txt")
    set(JUCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/External/JUCE")
endif()

if(DEFINED JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin formats
#
# iPadOS 17 only loads AUv3 app extensions, so that is the format that matters.
# The desktop formats exist purely so the same code can be auditioned and
# debugged from a normal DAW without an iOS device.
#
# On iOS the AUv3 extension cannot be installed on its own: it has to ship
# inside a container app. JUCE wires `XCODE_EMBED_APP_EXTENSIONS` from the
# Standalone target to the AUv3 target, so requesting both is what turns the
# build into something that can be packaged as an .ipa.
# ---------------------------------------------------------------------------
if(IOS)
    set(WOB_FORMATS AUv3 Standalone)
elseif(APPLE)
    set(WOB_FORMATS AU AUv3 Standalone VST3)
else()
    set(WOB_FORMATS Standalone VST3)
endif()

juce_add_plugin(MKWOPLER
    COMPANY_NAME                  "Wobble Audio"
    BUNDLE_ID                     "com.wobbleaudio.mkwopler"
    PLUGIN_MANUFACTURER_CODE      Wobl
    PLUGIN_CODE                   MKWP
    PRODUCT_NAME                  "MK WOPLER"
    DESCRIPTION                   "Multiband wobbling distorting filter"
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              TRUE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   FALSE
    COPY_PLUGIN_AFTER_BUILD       FALSE
    HARDENED_RUNTIME_ENABLED      TRUE
    IPHONEOS_DEPLOYMENT_TARGET    "17.0"
    MICROPHONE_PERMISSION_ENABLED FALSE
    FORMATS                       ${WOB_FORMATS})

target_sources(MKWOPLER PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/WobbleLookAndFeel.cpp
    Source/ui/Panels.cpp
    Source/dsp/Params.cpp
    Source/dsp/Filters.cpp
    Source/dsp/Lfo.cpp
    Source/dsp/Follower.cpp
    Source/dsp/Engine.cpp)

target_include_directories(MKWOPLER PRIVATE Source)

target_compile_definitions(MKWOPLER PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(MKWOPLER
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Optional CI signing hooks.
#
# Xcode manages two separate provisioning profiles for an AUv3 bundle: one for
# the container app and one for the extension, so a single global
# PROVISIONING_PROFILE_SPECIFIER cannot work. Passing these cache variables lets
# CI attach the right profile to each target without disturbing local builds,
# which leave them unset.
# ---------------------------------------------------------------------------
if(DEFINED WOB_APP_PROVISIONING_PROFILE AND TARGET MKWOPLER_Standalone)
    set_target_properties(MKWOPLER_Standalone PROPERTIES
        XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${WOB_APP_PROVISIONING_PROFILE}")
endif()

if(DEFINED WOB_AUV3_PROVISIONING_PROFILE AND TARGET MKWOPLER_AUv3)
    set_target_properties(MKWOPLER_AUv3 PROPERTIES
        XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${WOB_AUV3_PROVISIONING_PROFILE}")
endif()

endif() # WOB_BUILD_PLUGIN

# ---------------------------------------------------------------------------
# Offline test harness for the DSP core (no plugin framework required).
# ---------------------------------------------------------------------------
if(WOB_BUILD_TESTS)
    add_executable(wobble_tests
        Tests/test_dsp.cpp
        Source/dsp/Params.cpp
        Source/dsp/Filters.cpp
        Source/dsp/Lfo.cpp
        Source/dsp/Follower.cpp
        Source/dsp/Engine.cpp)

    set_target_properties(wobble_tests PROPERTIES CXX_STANDARD 17)
    target_compile_definitions(wobble_tests PRIVATE _CRT_SECURE_NO_WARNINGS)

    if(MSVC)
        target_compile_options(wobble_tests PRIVATE /W3 /O2)
    else()
        target_compile_options(wobble_tests PRIVATE -O2 -Wall)
    endif()

    enable_testing()
    add_test(NAME dsp COMMAND wobble_tests)
endif()
