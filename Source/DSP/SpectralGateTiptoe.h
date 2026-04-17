#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include <chrono>

class SpectralGateTiptoe
{
public:
    static constexpr int kFFTOrder = 11;
    static constexpr int kFFTSize = 1 << kFFTOrder;    // 2048
    static constexpr int kHopSize = kFFTSize / 2;       // 1024 (50% overlap)
    static constexpr int kNumBins = kFFTSize / 2 + 1;   // 1025
    static constexpr int kFFTMask = kFFTSize - 1;        // bitmask for power-of-2 wrap

    SpectralGateTiptoe();

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

    // Latency measurement
    float getLastProcessingTimeMs() const;

    // Parameters
    void setThreshold(float thresholdMultiplier);
    void setReduction(float reductionDB);

    // Visualisation: copy the most recent input frame magnitude spectrum.
    // Lock-free double-buffered snapshot written from processFrame() on the
    // audio thread and read from the UI thread. `out` is resized to kNumBins.
    void copyInputMagnitudes(std::vector<float>& out) const;

    // Sample rate the DSP was prepared with (0 if prepare() hasn't run yet).
    double getSampleRate() const { return sampleRate_; }

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

    // Timing
    float lastProcessingTimeMs_ = 0.0f;

    // Visualisation snapshots — ping-pong double buffer of per-bin magnitudes
    // for the most recent processed frame.
    std::array<std::vector<float>, 2> inputMagSnapshots_;
    mutable std::atomic<int> inputMagWriteIndex_ { 0 };

    // Cached sample rate from prepare() — used by the editor to label axes.
    double sampleRate_ = 0.0;

    // Parameters
    float thresholdMultiplier_ = 1.5f;
    float thresholdSq_ = 1.5f * 1.5f; // thresholdMultiplier^2, precomputed
    float reductionGain_ = 0.01f;

    void processFrame();
    void learnFrame(const float* frameData);
};
