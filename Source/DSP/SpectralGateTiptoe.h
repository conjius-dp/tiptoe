#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>
#include <chrono>

class SpectralGateTiptoe
{
public:
    // FFT size chosen as the lowest power of two that still gives adequate
    // bin resolution for broadband noise (86 Hz @ 44.1 kHz). Algorithmic
    // latency ≈ kFFTSize samples (≈ 11.6 ms at 44.1 kHz) so the DAW can
    // delay-compensate. Going lower (256) starts to smear low-freq noise.
    //
    // 75 % overlap (hop = size/4) is chosen over the classic 50 % for two
    // reasons:
    //   1) four analysis frames contribute to every output sample, so the
    //      Hann cross-fade is smoother — eliminates residual "chirpy"
    //      modulation from sudden per-bin gain changes.
    //   2) the parameter smoother and per-bin temporal gate advance 4×
    //      more often per second, so attack/release ms maps cleanly to
    //      wall-clock time and automation feels continuous.
    // COLA sum for Hann at 75 % overlap is 2.0 across all samples, so we
    // divide overlap-added output by 2.0 to preserve unity gain.
    static constexpr int kFFTOrder = 9;
    static constexpr int kFFTSize = 1 << kFFTOrder;    // 512
    static constexpr int kHopSize = kFFTSize / 4;       // 128 (75% overlap)
    static constexpr int kNumBins = kFFTSize / 2 + 1;   // 257
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

    // Parameters. Both setters set a target for an internal smoother so
    // DAW automation / knob drags don't produce frame-level granularity at
    // the FFT hop rate. The smoother advances one hop's worth of samples
    // per processFrame() call, with a 30 ms ramp time.
    void setThreshold(float thresholdMultiplier);
    void setReduction(float reductionDB);

    // Effective (post-smoothing) parameter values — read the smoother's
    // current value without advancing it. Primary use is tests; the UI also
    // reads these so its threshold-line tracks what the DSP actually applies.
    float getEffectiveThresholdMultiplier() const;
    float getEffectiveReductionGain() const;

    // Visualisation: copy the most recent input frame magnitude spectrum.
    // Lock-free double-buffered snapshot written from processFrame() on the
    // audio thread and read from the UI thread. `out` is resized to kNumBins.
    void copyInputMagnitudes(std::vector<float>& out) const;

    // Visualisation: copy the current noise-profile snapshot. Before and
    // after learning this is the final averaged profile; during learning it
    // is the in-progress running average, published after every learn frame.
    void copyNoiseProfile(std::vector<float>& out) const;

    // Sample rate the DSP was prepared with (0 if prepare() hasn't run yet).
    double getSampleRate() const { return sampleRate_; }

    // ───────── Artifact-reduction / temporal-gating introspection ─────────
    // Current smoothed gain applied to each bin (per-bin gate state). Lives
    // in [reductionGain .. 1]. 1 = fully open. Initialised to 1 on reset().
    float getBinGainState(int bin) const;

    // Per-bin coefficient of variation (stddev / mean) of the noise magnitude
    // measured during learning. 0 before learning; non-negative after.
    float getBinVolatility(int bin) const;

    // Attack / release times derived from per-bin volatility. Volatile bins
    // get shorter time constants (gate reacts faster); stable bins get
    // longer ones (smoother output). Defaults before learning.
    float getBinAttackMs(int bin) const;
    float getBinReleaseMs(int bin) const;

    // Pure-function soft-knee gain: returns the gain factor to apply to a
    // bin with magSq, given the bin's noise-threshold squared magnitude and
    // a reduction gain. Smoothly interpolates between reductionGain (well
    // below threshold) and 1.0 (well above). Exposed as a static so tests
    // can validate the interpolation shape without setting up a gate.
    static float softKneeGain(float magSq, float thresholdMagSq, float reductionGain);

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
    // for the most recent processed (or learned) frame.
    std::array<std::vector<float>, 2> inputMagSnapshots_;
    mutable std::atomic<int> inputMagWriteIndex_ { 0 };

    // Double-buffered noise profile snapshot. Kept in sync with noiseProfile_
    // on stopLearning() AND published incrementally on every learn frame
    // (running average) so the UI can watch the profile form.
    std::array<std::vector<float>, 2> noiseSnapshots_;
    mutable std::atomic<int> noiseWriteIndex_ { 0 };

    // Cached sample rate from prepare() — used by the editor to label axes.
    double sampleRate_ = 0.0;

    // Parameters
    float thresholdMultiplier_ = 1.5f;
    float thresholdSq_ = 1.5f * 1.5f; // thresholdMultiplier^2, precomputed
    float reductionGain_ = 0.01f;

    // Smoothed targets, advanced one hop per processFrame() so parameter
    // changes glide over ~30 ms instead of stepping at the hop rate.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> thresholdSmoothed_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> reductionGainSmoothed_;

    static constexpr double kParamRampSeconds = 0.03;

    // ───────── Artifact-reduction state ─────────
    //
    // Per-bin gate-gain state, updated once per processFrame with asymmetric
    // attack / release. Initialised to 1.0 (gate open).
    std::vector<float> gainState_;

    // Per-bin exponential smoothing coefficients (alpha in [0,1] applied at
    // the hop rate). Computed from per-bin time constants on prepare() and
    // refreshed on stopLearning() from learned volatility.
    std::vector<float> attackCoef_;
    std::vector<float> releaseCoef_;

    // Per-bin time constants — exposed via the ms getters. Start at the
    // defaults below; after learning, each bin gets its own ms value scaled
    // by the measured coefficient of variation of that bin's noise level.
    std::vector<float> attackMs_;
    std::vector<float> releaseMs_;

    // Running magnitude-squared sum per bin during learning, used with
    // noiseAccumulator_ to compute the coefficient of variation on stop.
    std::vector<double> noiseMagSqAccumulator_;

    // Coefficient of variation measured at stopLearning (stddev / mean).
    // 0 by default; populated after learning a non-silent profile.
    std::vector<float> volatility_;

    // Spectral-smoothing scratch buffer (smoothed bin gains across
    // neighbours). Sized to kNumBins on prepare. Avoids per-frame alloc.
    std::vector<float> gainSmoothScratch_;

    static constexpr float kOverSubtractionFactor = 1.5f;
    static constexpr float kSoftKneeHalfWidthSq = 4.0f; // ± (4×=6 dB) around threshold
    // Default time constants: chosen so a single FFT hop (~23 ms at the
    // 2048/44.1k default) doesn't already complete the transition — that's
    // what makes per-bin smoothing audible as a fade rather than a step.
    static constexpr float kDefaultAttackMs  = 30.0f;
    static constexpr float kDefaultReleaseMs = 120.0f;
    static constexpr int   kSpectralSmoothHalfWidth = 1; // 3-bin box filter

    // Convert a time constant in ms to a one-pole smoother coefficient
    // that advances once per FFT hop. 1.0 = instant, 0.0 = frozen.
    float msToHopCoef(float ms) const;

    // Refresh attackCoef_ / releaseCoef_ from attackMs_ / releaseMs_.
    // Called on prepare() and when learning updates the time constants.
    void refreshTemporalCoefs();

    void processFrame();
    void learnFrame(const float* frameData);
};
