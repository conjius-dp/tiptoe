cmake_minimum_required(VERSION 3.22)

project(Tiptoe VERSION 0.1.5)

add_subdirectory(JUCE)

# --- DSP Library (shared between plugin and tests) ---
add_library(TiptoeDSP STATIC
    Source/DSP/SpectralGateTiptoe.cpp
)

target_include_directories(TiptoeDSP PUBLIC Source)

target_link_libraries(TiptoeDSP
    PUBLIC
        juce::juce_dsp
)

target_compile_definitions(TiptoeDSP
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
)

# --- Plugin ---
juce_add_plugin(Tiptoe
    COMPANY_NAME "tiptoe"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    PLUGIN_MANUFACTURER_CODE Tpto
    PLUGIN_CODE Tpto
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "tiptoe"
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_sources(Tiptoe
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/KnobDesign.h
)

juce_add_binary_data(TiptoeBinaryData
    SOURCES
        Assets/conjius-avatar-transparent-bg.png
        Assets/tiptoe-logo-orange.png
        Assets/Inconsolata-Bold.ttf
        Assets/Inconsolata-Regular.ttf)

target_compile_definitions(Tiptoe
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Tiptoe
    PRIVATE
        TiptoeDSP
        TiptoeBinaryData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Tests ---
add_executable(TiptoeTests
    Tests/TestSpectralGateTiptoe.cpp
)

target_link_libraries(TiptoeTests
    PRIVATE
        TiptoeDSP
        juce::juce_core
)

target_compile_definitions(TiptoeTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
)
