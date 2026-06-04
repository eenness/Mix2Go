cmake_minimum_required(VERSION 3.23.1)

# macOS architecture settings MUST come before project() and FetchContent so
# that all sub-builds (opus, JUCE) inherit the same architectures.
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum OS X deployment version" FORCE)
  set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Architectures" FORCE)
  set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "11.0" CACHE STRING "arm64 minimum deployment target" FORCE)
endif()

# General setup
set(PROJECT_NAME "Mix2Go")
set(CURRENT_VERSION "0.0.1")
set(PRODUCT_NAME "Mix2Go")
set(COMPANY_NAME "Viator DSP")
set(FORMATS Standalone AU VST3)

project(Mix2Go VERSION 0.0.1)

# ── libopus (FetchContent) ────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    opus
    GIT_REPOSITORY https://gitlab.xiph.org/xiph/opus.git
    GIT_TAG        v1.5.2
    GIT_SHALLOW    TRUE
)
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(opus)
# ─────────────────────────────────────────────────────────────────────────────

add_subdirectory(modules/JUCE)

# Project configuration
set(CMAKE_CXX_STANDARD 20)   # matches cxx_std_20 required by SharedCode

if (UNIX AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
 find_package(PkgConfig REQUIRED)
 pkg_check_modules(WEBKIT2 REQUIRED webkit2gtk-4.0)

 include_directories(${WEBKIT2_INCLUDE_DIRS})
 link_directories(${WEBKIT2_LIBRARY_DIRS})
 add_definitions(${WEBKIT2_CFLAGS_OTHER})
endif()


# Append custom module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules/cmake-include/sudara-cmake")

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
 set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE INTERNAL "")
endif()

# Define the plugin
juce_add_plugin("Mix2Go"
        COMPANY_NAME "${COMPANY_NAME}"
        BUNDLE_ID "${BUNDLE_ID}"
        COPY_PLUGIN_AFTER_BUILD TRUE
        PLUGIN_MANUFACTURER_CODE Vdsp
        PLUGIN_CODE CmCs
        FORMATS ${FORMATS}
        PRODUCT_NAME "${PRODUCT_NAME}"
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        IS_SYNTH FALSE
)

add_compile_definitions(JUCE_PROJECT_NAME="${PROJECT_NAME}")

# Define SharedCode as an INTERFACE library
add_library(SharedCode INTERFACE)

# Set compile features for SharedCode
target_compile_features(SharedCode INTERFACE cxx_std_20)

# Include directories and compile definitions for SharedCode
target_include_directories(SharedCode INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
        "${CMAKE_CURRENT_SOURCE_DIR}/modules/JUCE/modules"
        "${CMAKE_BINARY_DIR}/AudioPluginData"
        "${opus_SOURCE_DIR}/include"
)

# Set JUCE flags for SharedCode
target_compile_definitions(SharedCode INTERFACE
        JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
        JUCE_WEB_BROWSER=1
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        VERSION="${CURRENT_VERSION}"
        JUCE_DISPLAY_SPLASH_SCREEN=0
        PRODUCT_NAME_WITHOUT_VERSION="CometChannelStrip"
)

# Add sources to the main project
file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h"
)

# Sources to main project
target_sources("${PROJECT_NAME}" PRIVATE ${SourceFiles})

# Assets setup
file(GLOB_RECURSE Assets "${CMAKE_CURRENT_SOURCE_DIR}/assets/*.*")

# Add the Assets binary data as a target
juce_add_binary_data(AudioPluginData SOURCES ${Assets})

# Link AudioPluginData to SharedCode
target_link_libraries(SharedCode INTERFACE AudioPluginData)

# Ensure AudioPluginData is built before the main project
add_dependencies(${PROJECT_NAME} AudioPluginData)

# Link libraries to main project
 target_link_libraries("${PROJECT_NAME}" PRIVATE
         SharedCode
         AudioPluginData
         opus
         juce::juce_audio_basics
         juce::juce_audio_devices
         juce::juce_audio_formats
         juce::juce_audio_plugin_client
         juce::juce_audio_processors
         juce::juce_audio_utils
         juce::juce_core
         juce::juce_data_structures
         juce::juce_dsp
         juce::juce_events
         juce::juce_graphics
         juce::juce_gui_basics
         juce::juce_gui_extra
         juce::juce_product_unlocking
         PUBLIC
         juce::juce_recommended_config_flags
         juce::juce_recommended_lto_flags
         juce::juce_recommended_warning_flags
 )

# Ensure the main project knows where its sources are
target_include_directories("${PROJECT_NAME}" PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/source"
)