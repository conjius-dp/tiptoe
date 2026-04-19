#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

// Single-band spectral gate with runtime-configurable FFT order.
// Same algorithm as the legacy SpectralGateTiptoe (Hann window, 75 %
// overlap-add, per-bin soft-knee gate with asymmetric attack/release
// time-constant smoothing, over-subtraction, 3-bin spectral smoothing)
// but every size is an instance member so the multi-band gate can
// spin up multiple instances at different FFT orders.
//
// Public surface is trimmed to what the multi-band wrapper needs:
//  - constructor(fftOrder), prepare, reset
//  - startLearning / stopLearning / isLearning / learnFromBlock
//  - processMono (in-place)
//  - setSensitivity / setReduction
//  - getFFTSize / getHopSize / getNumBins / getNoiseProfile / getSampleRate
//
// No visualization double-buffers, no per-bin introspection, no
// effective-value accessors — those lived on the old class for UI
// plumbing and can be re-added at the MultiBandGate level if needed.
class BandGate
{
public:
    // FFT order is clamped to [4, 11] i.e. FFT sizes 16 to 2048.
    // Order 4  — tiny frame used in the low band at decimated rate.
    // Order 11 — headroom; no caller needs this today.
    explicit BandGate(int fftOrder);

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // --- Noise learning ------------------------------------------------
    void startLearning();
    void stopLearning();
    bool isLearning() const { return learning_; }
    void learnFromBlock(const float* samples, int numSamples);
    const std::vector<float>& getNoiseProfile() const { return noiseProfile_; }

    // --- Processing ----------------------------------------------------
    void processMono(float* samples, int numSamples);

    // --- Parameter setters (smoothed over ~30 ms wall-clock) -----------
    void setSensitivity(float sensitivityMultiplier);
    void setReduction(float reductionDB);

    // --- Accessors -----------------------------------------------------
    int getFFTOrder() const noexcept { return fftOrder_; }
    int getFFTSize()  const noexcept { return fftSize_;  }
    int getHopSize()  const noexcept { return hopSize_;  }
    int getNumBins()  const noexcept { return numBins_;  }
    double getSampleRate() const noexcept { return sampleRate_; }

    // Visualization: copy the most recent input-frame per-bin magnitudes.
    // Lock-free double-buffered snapshot written by processFrame on the
    // audio thread, read by the UI thread. `out` is resized to numBins_.
    void copyInputMagnitudes(std::vector<float>& out) const;
    // Same shape, but for the current learned noise profile (during
    // learning this is the running average, so the UI can animate the
    // profile forming in real time).
    void copyNoiseProfile(std::vector<float>& out) const;

    // Pure soft-knee gain, exposed for testing.
    static float softKneeGain(float magSq, float thresholdMagSq, float reductionGain);

private:
    // --- Sizes (fixed at construction) ---------------------------------
    int fftOrder_;
    int fftSize_;
    int hopSize_;
    int numBins_;
    int fftMask_;

    juce::dsp::FFT fft_;

    // --- Ring buffers + window -----------------------------------------
    std::vector<float> window_;
    std::vector<float> inputFifo_;
    std::vector<float> outputFifo_;
    int inputFifoIndex_ = 0;
    int hopCounter_ = 0;

    std::vector<float> fftWorkspace_;    // size = 2 * fftSize_
    float invWindowSum_ = 1.0f;          // 0.5 for Hann 75 % overlap

    // --- Noise profile -------------------------------------------------
    std::vector<float>  noiseProfile_;
    std::vector<float>  noiseProfileSq_;
    std::vector<double> noiseAccumulator_;
    std::vector<double> noiseMagSqAccumulator_;
    int  noiseFrameCount_ = 0;
    bool learning_ = false;
    bool hasNoiseProfile_ = false;

    // Learning input FIFO
    std::vector<float> learnFifo_;
    int learnFifoIndex_ = 0;

    // --- Parameters ----------------------------------------------------
    double sampleRate_ = 0.0;

    float sensitivityMultiplier_ = 1.0f;
    float reductionGain_ = 0.01f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> sensitivitySmoothed_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reductionGainSmoothed_;

    static constexpr double kParamRampSeconds = 0.03;

    // --- Artifact-reduction state --------------------------------------
    std::vector<float>  gainState_;
    std::vector<float>  attackCoef_;
    std::vector<float>  releaseCoef_;
    std::vector<float>  attackMs_;
    std::vector<float>  releaseMs_;
    std::vector<float>  volatility_;
    std::vector<float>  gainSmoothScratch_;
    std::vector<float>  spectralSmoothScratch_;

    // --- Visualization snapshots (lock-free double buffer) -----------
    std::array<std::vector<float>, 2>  inputMagSnapshots_;
    mutable std::atomic<int>           inputMagWriteIndex_ { 0 };
    std::array<std::vector<float>, 2>  noiseSnapshots_;
    mutable std::atomic<int>           noiseWriteIndex_    { 0 };

    static constexpr float kOverSubtractionFactor = 1.5f;
    static constexpr float kSoftKneeHalfWidthSq   = 4.0f;
    static constexpr float kDefaultAttackMs   = 30.0f;
    static constexpr float kDefaultReleaseMs  = 120.0f;
    static constexpr int   kSpectralSmoothHalfWidth = 1;

    // --- Internals -----------------------------------------------------
    float msToHopCoef(float ms) const;
    void  refreshTemporalCoefs();
    void  processFrame();
    void  learnFrame(const float* frameData);

    // Helpers for construction.
    static int clampOrder(int order) noexcept
    {
        return juce::jlimit(4, 11, order);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BandGate)
};
