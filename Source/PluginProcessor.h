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

    // Noise learning control
    void startLearning();
    void stopLearning();
    bool isLearning() const;

    // Processing latency (max of both channels)
    float getLastProcessingTimeMs() const;

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
