cmake_minimum_required(VERSION 3.22)

project(Denoiser VERSION 0.1.0)

include(FetchContent)

# --- JUCE ---
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.12
)
FetchContent_MakeAvailable(JUCE)

# --- DSP Library (shared between plugin and tests) ---
add_library(DenoiserDSP STATIC
    Source/DSP/SpectralGateDenoiser.cpp
)

target_include_directories(DenoiserDSP PUBLIC Source)

target_link_libraries(DenoiserDSP
    PUBLIC
        juce::juce_dsp
)

target_compile_definitions(DenoiserDSP
    PUBLIC
        JUCE_STANDALONE_APPLICATION=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# --- Plugin ---
juce_add_plugin(Denoiser
    COMPANY_NAME "Denoiser"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Dnsr
    PLUGIN_CODE Dnsr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Denoiser"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Denoiser
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(Denoiser
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Denoiser
    PRIVATE
        DenoiserDSP
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.2
)
FetchContent_MakeAvailable(Catch2)

add_executable(DenoiserTests
    Tests/TestSpectralGateDenoiser.cpp
)

target_link_libraries(DenoiserTests
    PRIVATE
        DenoiserDSP
        Catch2::Catch2WithMain
)

include(CTest)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(DenoiserTests)
