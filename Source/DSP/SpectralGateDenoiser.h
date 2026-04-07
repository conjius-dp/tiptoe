#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>

class SpectralGateDenoiser
{
public:
    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize = 1 << kFFTOrder;    // 2048
    static constexpr int kHopSize = kFFTSize / 2;       // 1024 (50% overlap)
    static constexpr int kNumBins = kFFTSize / 2 + 1;   // 1025
    static constexpr int kFFTMask = kFFTSize - 1;        // bitmask for power-of-2 wrap

    SpectralGateDenoiser();

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Noise learning
    void startLearning();
    void stopLearning();
    bool isLearning() const;
    void learnFromBlock(const float* samples, int numSamples);
    const std::vector<float>& getNoiseProfile() const;

    // Processing
    void processMono(float* samples, int numSamples);

    // Parameters
    void setThreshold(float thresholdMultiplier);
    void setReduction(float reductionDB);

private:
    juce::dsp::FFT fft;

    std::vector<float> window;
    std::vector<float> inputFifo;
    std::vector<float> outputFifo;
    int inputFifoIndex = 0;
    int hopCounter = 0;

    std::vector<float> fftWorkspace;

    // Noise profile (stored as squared magnitudes for comparison without sqrt)
    std::vector<float> noiseProfile_;
    std::vector<float> noiseProfileSq_; // noiseProfile[i]^2, precomputed
    std::vector<double> noiseAccumulator_;
    int noiseFrameCount_ = 0;
    bool learning_ = false;
    bool hasNoiseProfile_ = false; // cached flag

    // Learning input buffer
    std::vector<float> learnFifo_;
    int learnFifoIndex_ = 0;

    // Precomputed constants
    float invWindowSum_ = 1.0f; // 1.0 / windowSum for multiply instead of divide

    // Parameters
    float thresholdMultiplier_ = 1.5f;
    float thresholdSq_ = 1.5f * 1.5f; // thresholdMultiplier^2, precomputed
    float reductionGain_ = 0.01f;

    void processFrame();
    void learnFrame(const float* frameData);
};
