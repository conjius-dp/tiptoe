#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/SpectralGateTiptoe.h"
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

    // Algorithmic latency in milliseconds — the buffering delay the FFT
    // overlap-add introduces. This is the number the DAW compensates for
    // and what the user hears vs. bypass.
    float getAlgorithmicLatencyMs() const
    {
        const double sr = gates[0].getSampleRate();
        if (sr <= 0.0) return 0.0f;
        return static_cast<float>(
            static_cast<double>(SpectralGateTiptoe::kFFTSize) / sr * 1000.0);
    }

    // Spectrum-graph accessors (read from the first channel — mono-safe view
    // for visualisation). Lock-free: audio thread writes a double-buffered
    // snapshot, UI reads the most recent one.
    void copyInputMagnitudes(std::vector<float>& out) const { gates[0].copyInputMagnitudes(out); }
    void copyNoiseProfile(std::vector<float>& out) const { gates[0].copyNoiseProfile(out); }
    const std::vector<float>& getNoiseProfile() const { return gates[0].getNoiseProfile(); }
    double getDspSampleRate() const { return gates[0].getSampleRate(); }
    static constexpr int getFFTSize() { return SpectralGateTiptoe::kFFTSize; }
    static constexpr int getNumBins() { return SpectralGateTiptoe::kNumBins; }

    // Editor size persistence
    std::atomic<int> editorWidth  { KnobDesign::defaultWidth };
    std::atomic<int> editorHeight { KnobDesign::defaultHeight };

private:
    juce::AudioProcessorValueTreeState apvts;
    SpectralGateTiptoe gates[2]; // one per stereo channel
    bool learning_ = false;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiptoeAudioProcessor)
};
