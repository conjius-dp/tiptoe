cmake_minimum_required(VERSION 3.22)

project(Tiptoe VERSION 0.1.17)

add_subdirectory(JUCE)

# --- DSP Library (shared between plugin and tests) ---
add_library(TiptoeDSP STATIC
    Source/DSP/SpectralGateTiptoe.cpp
    Source/DSP/BandGate.cpp
    Source/DSP/MultiBandGate.cpp
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
        Source/SpectrumGraph.cpp
        Source/KnobDesign.h
        Source/SpectrumGraph.h
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

# ── Aggressive binary-size reduction ──────────────────────────────────
# Hide every symbol that isn't part of the VST3/AU/Standalone C ABI, and
# fold identical / unreferenced functions at link time. Combined with
# juce_recommended_lto_flags these cut ~20 % off the final binary before
# the post-build strip runs.
target_compile_options(Tiptoe PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
        -fvisibility=hidden
        -fvisibility-inlines-hidden
        -ffunction-sections
        -fdata-sections>)

target_link_options(Tiptoe PRIVATE
    $<$<PLATFORM_ID:Darwin>:-Wl,-dead_strip -Wl,-x>
    $<$<PLATFORM_ID:Linux>:-Wl,--gc-sections -Wl,--strip-all>)

if(MSVC)
    # Remove unreferenced code/data + fold identical COMDATs.
    target_link_options(Tiptoe PRIVATE /OPT:REF /OPT:ICF)
endif()

# Strip the final plugin binary after the build. This is cheap
# (runs once per format) and saves ~1 MB per binary by dropping debug
# symbol tables and private name strings. Release builds only.
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR NOT CMAKE_BUILD_TYPE)
    foreach(kind VST3 AU Standalone)
        if(TARGET Tiptoe_${kind})
            if(APPLE)
                # Strip, then re-apply the ad-hoc code signature. Without
                # the re-sign, macOS 14+ refuses to dlopen the bundle
                # (SIGKILL, "Code Signature Invalid") because strip mutates
                # the binary after JUCE's post-link ad-hoc sign.
                add_custom_command(TARGET Tiptoe_${kind} POST_BUILD
                    COMMAND ${CMAKE_STRIP} -x -S
                        $<TARGET_FILE:Tiptoe_${kind}>
                    COMMAND codesign --force --sign - --timestamp=none
                        $<TARGET_BUNDLE_DIR:Tiptoe_${kind}>
                    COMMENT "Stripping + re-signing Tiptoe_${kind}")
            elseif(UNIX)
                add_custom_command(TARGET Tiptoe_${kind} POST_BUILD
                    COMMAND ${CMAKE_STRIP} --strip-all
                        $<TARGET_FILE:Tiptoe_${kind}>
                    COMMENT "Stripping Tiptoe_${kind}")
            endif()
        endif()
    endforeach()
endif()

# --- Tests ---
add_executable(TiptoeTests
    Tests/TestSpectralGateTiptoe.cpp
    Tests/TestLayoutSizes.cpp
    Tests/TestBypassButton.cpp
    Tests/TestCrossover.cpp
    Tests/TestResampler.cpp
    Tests/TestBandGate.cpp
    Tests/TestMultiBandGate.cpp
)

target_include_directories(TiptoeTests PRIVATE Source)

target_link_libraries(TiptoeTests
    PRIVATE
        TiptoeDSP
        juce::juce_core
)

target_compile_definitions(TiptoeTests
    PRIVATE
        JUCE_STANDALONE_APPLICATION=1
)

# --- Screenshot CLI ---
# Headless renderer for the plugin editor — used by CI to generate the README image.
# Links against Tiptoe_SharedCode so all plugin macros (JucePlugin_Name etc.) and
# sources are available without re-declaring them here.
add_executable(TiptoeScreenshot
    Tools/Screenshot.cpp
)

target_include_directories(TiptoeScreenshot PRIVATE Source)

target_link_libraries(TiptoeScreenshot
    PRIVATE
        Tiptoe
        TiptoeBinaryData
        juce::juce_audio_utils
        juce::juce_gui_basics
)
