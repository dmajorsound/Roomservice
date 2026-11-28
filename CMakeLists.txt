cmake_minimum_required(VERSION 3.22)
# macOS: must be set before project(); ignored on other systems.
# Universal (Apple Silicon + Intel). For a faster Intel-only build: -DCMAKE_OSX_ARCHITECTURES=x86_64
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(Roomservice VERSION 1.3.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: -DJUCE_DIR=/path/to/JUCE (default: ../juce next to this folder)
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../juce" CACHE PATH "JUCE source folder")
add_subdirectory(${JUCE_DIR} juce EXCLUDE_FROM_ALL)

set(RS_FORMATS VST3)
if(APPLE)
  list(APPEND RS_FORMATS AU)
endif()

juce_add_plugin(Roomservice
  COMPANY_NAME "dmajorsound"
  COMPANY_WEBSITE "https://dmajorsound.com"
  PLUGIN_MANUFACTURER_CODE Dmjs
  PLUGIN_CODE AhhS              # (a new plug-in: stereo and 5.1 in one; not the id of the stereo-only Ahh up to 1.0.x)
  FORMATS ${RS_FORMATS}
  PRODUCT_NAME "Ahh"
  VERSION 1.3.3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Fx Reverb Spatial
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD FALSE)

# The plug-in's own font (Mulish Regular and SemiBold, SIL Open Font License, resources/fonts/OFL-Mulish.txt; with two
# characters from Arimo, see tools/build_fonts.py) is built in,
# so the text looks the same on every machine and never depends on installed fonts.
juce_add_binary_data(RoomserviceData NAMESPACE RsData HEADER_NAME RsData.h
  SOURCES resources/fonts/Mulish-Regular.ttf resources/fonts/Mulish-SemiBold.ttf resources/Ahh_icon.svg)
set_target_properties(RoomserviceData PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB RS_GEN ${CMAKE_CURRENT_SOURCE_DIR}/gen/*.cpp)
target_sources(Roomservice PRIVATE
  src/PluginProcessor.cpp src/PluginEditor.cpp src/JuceGfx.cpp src/UpdateCheck.cpp src/rt.cpp src/engine.cpp
  third_party/wdl/fft.c ${RS_GEN})
target_include_directories(Roomservice PRIVATE src gen third_party/wdl)

target_compile_definitions(Roomservice PUBLIC
  WDL_FFT_REALSIZE=8
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)
option(RS_MINIMAL_X11 "Linux: build without the Xrandr/Xinerama/Xcursor/XInput2/Xrender dev headers" OFF)
if(UNIX AND NOT APPLE AND RS_MINIMAL_X11)
  target_compile_definitions(Roomservice PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0
                                                JUCE_USE_XINPUT=0 JUCE_USE_XRENDER=0)
endif()

# bit-exact arithmetic with the JSFX: no FMA contraction, no fast-math
if(MSVC)
  target_compile_options(Roomservice PRIVATE /fp:precise /bigobj)
else()
  target_compile_options(Roomservice PRIVATE -ffp-contract=off -fno-fast-math)
endif()

target_link_libraries(Roomservice PRIVATE
  RoomserviceData
  juce::juce_audio_processors juce::juce_gui_basics
  PUBLIC juce::juce_recommended_config_flags)

# CLAP (via clap-juce-extensions, fetched next to JUCE; skipped if missing)
set(CLAP_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../clap-juce-extensions" CACHE PATH "clap-juce-extensions checkout")
if(EXISTS "${CLAP_JUCE_DIR}/CMakeLists.txt")
  add_subdirectory(${CLAP_JUCE_DIR} clap-juce EXCLUDE_FROM_ALL)
  clap_juce_extensions_plugin(TARGET Roomservice
    CLAP_ID "com.dmajorsound.ahh"
    CLAP_FEATURES audio-effect reverb stereo mono)
endif()

# The build's self-test of the update download (see tests/update_download.cpp): the same code as in the plug-in
juce_add_console_app(ahh_update_test PRODUCT_NAME "ahh_update_test")
target_sources(ahh_update_test PRIVATE tests/update_download.cpp src/UpdateCheck.cpp)
target_compile_definitions(ahh_update_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_VersionString="0.0.0")
target_link_libraries(ahh_update_test PRIVATE juce::juce_core juce::juce_data_structures PUBLIC juce::juce_recommended_config_flags)
