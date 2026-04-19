#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/MultiBandGate.h"
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

    // True when the plugin is running in HQ (single-band, 11.6 ms) mode.
    // False ⇒ multi-band realtime (3.67 ms).
    bool isHQMode() const
    {
        auto* v = apvts.getRawParameterValue("hq");
        return v != nullptr && v->load() >= 0.5f;
    }

    // Latency in INPUT-rate samples for the currently-active mode. The
    // editor timer watches this and calls setLatencySamples() when the
    // toggle flips.
    int getCurrentLatencyInSamples() const
    {
        return isHQMode() ? static_cast<int>(SpectralGateTiptoe::kFFTSize)
                          : hqGates[0].getSampleRate() > 0.0
                              ? gates[0].getLatencyInSamples()
                              : 162; // fallback before prepare()
    }

    // Algorithmic latency in milliseconds — total buffering delay of the
    // currently-selected mode.
    float getAlgorithmicLatencyMs() const
    {
        const double sr = gates[0].getSampleRate();
        if (sr <= 0.0) return 0.0f;
        const int samples = isHQMode() ? static_cast<int>(SpectralGateTiptoe::kFFTSize)
                                       : gates[0].getLatencyInSamples();
        return static_cast<float>(static_cast<double>(samples) / sr * 1000.0);
    }

    // Spectrum-graph accessors — always sourced from the HQ gate,
    // regardless of active mode. The HQ gate runs in parallel on the
    // audio thread in realtime mode (input is fed to a scratch buffer;
    // its DSP output is discarded, only the published FFT snapshot is
    // read by the UI). This gives the user a single consistent
    // visualization — same FFT size, same bin count, same dB scale —
    // whether the plugin is processing through the multi-band path or
    // the single-band one.
    void copyInputMagnitudes(std::vector<float>& out) const { hqGates[0].copyInputMagnitudes(out); }
    void copyNoiseProfile   (std::vector<float>& out) const { hqGates[0].copyNoiseProfile   (out); }
    const std::vector<float>& getNoiseProfile() const        { return hqGates[0].getNoiseProfile(); }
    double getDspSampleRate() const { return gates[0].getSampleRate(); }
    int getFFTSize() const { return SpectralGateTiptoe::kFFTSize; }
    int getNumBins() const { return SpectralGateTiptoe::kNumBins; }

    // Editor size persistence
    std::atomic<int> editorWidth  { KnobDesign::defaultWidth };
    std::atomic<int> editorHeight { KnobDesign::defaultHeight };

private:
    // Multi-band config. 2 kHz crossover, 8× low-band decimation, low
    // FFT 16 at decimated rate, high FFT 128 at full rate. Reported
    // plugin latency ≈ 3.67 ms at 44.1 kHz.
    static constexpr MultiBandGate::Config kMultiBandConfig {
        2000.0f, 8, 4, 7
    };

    juce::AudioProcessorValueTreeState apvts;
    MultiBandGate gates[2] { MultiBandGate{kMultiBandConfig},
                             MultiBandGate{kMultiBandConfig} }; // realtime per-channel
    SpectralGateTiptoe hqGates[2];                              // HQ mode per-channel
    bool learning_ = false;
    bool prevHQMode_ = false; // for detecting mode-change → setLatencySamples

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiptoeAudioProcessor)
};
