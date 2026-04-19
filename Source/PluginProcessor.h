#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/MultiBandGate.h"
#include "KnobDesign.h"

class TiptoeAudioProcessor : public juce::AudioProcessor
{
public:
    TiptoeAudioProcessor();
    ~TiptoeAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Host-integrated bypass — returning our APVTS bool parameter here
    // lets DAWs drive the plugin's bypass state through automation without
    // a custom handshake. processBlock early-returns on this param too,
    // so the in-plugin power button and host bypass behave identically.
    juce::AudioParameterBool* getBypassParameter() const override
    {
        return dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass"));
    }

    // Noise learning control
    void startLearning();
    void stopLearning();
    bool isLearning() const;

    // Wall-clock time the last processBlock took (CPU load indicator).
    float getLastProcessingTimeMs() const;

    // Algorithmic latency in milliseconds — the total buffering delay of
    // the multi-band pipeline (crossover + decimate + FFT + interpolate,
    // delay-aligned with the high band). This is the number the DAW
    // compensates for and what the user hears vs. bypass.
    float getAlgorithmicLatencyMs() const
    {
        const double sr = gates[0].getSampleRate();
        if (sr <= 0.0) return 0.0f;
        return static_cast<float>(
            static_cast<double>(gates[0].getLatencyInSamples()) / sr * 1000.0);
    }

    // Spectrum-graph accessors (read from the first channel — mono-safe view
    // for visualisation). Lock-free: audio thread writes a double-buffered
    // snapshot, UI reads the most recent one. Multi-band mode shows the
    // HIGH band's FFT (wider visible range at meaningful resolution).
    void copyInputMagnitudes(std::vector<float>& out) const { gates[0].copyInputMagnitudes(out); }
    void copyNoiseProfile(std::vector<float>& out) const { gates[0].copyNoiseProfile(out); }
    const std::vector<float>& getNoiseProfile() const { return gates[0].getHighBandNoiseProfile(); }
    double getDspSampleRate() const { return gates[0].getSampleRate(); }
    int getFFTSize() const { return gates[0].getVisualizationFFTSize(); }
    int getNumBins() const { return gates[0].getVisualizationNumBins(); }

    // Editor size persistence
    std::atomic<int> editorWidth  { KnobDesign::defaultWidth };
    std::atomic<int> editorHeight { KnobDesign::defaultHeight };

private:
    // Static config for the multi-band gate. 2 kHz crossover, 8× low-band
    // decimation, low FFT 16 at decimated rate (172 Hz bins covering
    // 0–2 kHz), high FFT 128 at full rate (344 Hz bins covering 0–22 kHz).
    // Reported plugin latency ≈ 3.7 ms at 44.1 kHz.
    static constexpr MultiBandGate::Config kMultiBandConfig {
        2000.0f, 8, 4, 7
    };

    juce::AudioProcessorValueTreeState apvts;
    MultiBandGate gates[2] { MultiBandGate{kMultiBandConfig},
                             MultiBandGate{kMultiBandConfig} }; // one per stereo channel
    bool learning_ = false;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiptoeAudioProcessor)
};
