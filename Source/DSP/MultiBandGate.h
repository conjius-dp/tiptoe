#pragma once

#include "BandGate.h"
#include "Crossover.h"
#include "Decimator.h"
#include "Interpolator.h"

#include <juce_dsp/juce_dsp.h>
#include <vector>

// Two-band spectral gate. Splits the signal at a configurable crossover
// frequency, runs a low-rate FFT on a decimated low band (fine time
// window, coarse bin spacing relative to the full-rate FFT at the same
// latency) and a high-rate FFT on the high band (coarse time coverage,
// broad bin spacing). Bands are delay-aligned and summed.
//
// Total latency in input-rate samples =
//   crossover IIR group delay (≈ 0)
//   + max( low-path latency, high-path latency )
// where:
//   low-path  = decimator FIR GD + FFT_low × D + interpolator FIR GD
//   high-path = FFT_high
//
// With the default config below this lands around 250–300 samples
// (≈ 5–7 ms at 44.1 kHz) — a substantial drop from single-band's
// FFT-512 floor of 11.6 ms.
class MultiBandGate
{
public:
    struct Config
    {
        float crossoverHz;
        int   decimationFactor;   // must divide the host block size cleanly
        int   lowFFTOrder;        // 4..11 (FFT sizes 16..2048) at the DECIMATED rate
        int   highFFTOrder;       // 4..11 at the FULL rate
    };

    explicit MultiBandGate(Config cfg);

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Noise learning is delegated to both band gates so each band's
    // profile comes from exactly its own bandpassed content.
    void startLearning();
    void stopLearning();
    bool isLearning() const { return learning_; }
    void learnFromBlock(const float* samples, int numSamples);

    // In-place processing. `numSamples` can be any positive integer;
    // internally the pipeline buffers any sub-D remainder.
    void processMono(float* samples, int numSamples);

    // Parameter control applies to both bands simultaneously. Per-band
    // tuning can be added later.
    void setSensitivity(float sensitivityMultiplier);
    void setReduction(float reductionDB);

    // Total algorithmic latency to report to the host. Computed in
    // prepare() from the Crossover + Decimator + lowBandGate FFT +
    // Interpolator + band-alignment delay.
    int getLatencyInSamples() const noexcept { return totalLatency_; }

    // Access the underlying band gates' noise profiles (mostly for tests).
    const std::vector<float>& getLowBandNoiseProfile () const { return lowBand_ .getNoiseProfile(); }
    const std::vector<float>& getHighBandNoiseProfile() const { return highBand_.getNoiseProfile(); }

    // Spectrum-graph hooks. The editor gets a SINGLE seamless curve
    // with bins that correspond to the high band's FFT grid (FFT 128 at
    // full rate → 65 bins at 344 Hz spacing covering 0–22 kHz). The
    // low band's FFT 16 at 1/8 rate lands on the SAME 344 Hz bin grid
    // over its 0–2.75 kHz range by design. We merge the two via
    // quadrature sum so the UI sees total energy per bin — no visible
    // seam at the crossover, no awareness there are bands underneath.
    void copyInputMagnitudes(std::vector<float>& out) const;
    void copyNoiseProfile   (std::vector<float>& out) const;

    int getVisualizationFFTSize() const noexcept { return highBand_.getFFTSize(); }
    int getVisualizationNumBins() const noexcept { return highBand_.getNumBins(); }

    double getSampleRate() const noexcept { return sampleRate_; }

private:
    const Config cfg_;

    // --- Pipeline -----------------------------------------------------
    Crossover    xo_;
    Decimator    dec_;
    BandGate     lowBand_;
    Interpolator up_;
    BandGate     highBand_;

    // --- Delay line for the high band (ring buffer) ------------------
    std::vector<float> highDelayBuf_;
    int highDelaySize_ = 0;
    int highDelayPos_  = 0;

    // --- Low-band output FIFO ----------------------------------------
    // The decimator / interpolator may not output exactly `numSamples`
    // per processMono call when the block isn't a multiple of D
    // (or during warm-up), so we push to a FIFO and drain at input rate.
    std::vector<float> lowFifo_;
    int lowFifoWrite_ = 0;
    int lowFifoRead_  = 0;
    int lowFifoSize_  = 0; // power of two; `mask` uses lowFifoSize_ - 1

    // --- Scratch buffers (resized in prepare, sized for maxBlockSize) --
    std::vector<float> lowFull_;
    std::vector<float> highFull_;
    std::vector<float> lowDec_;
    std::vector<float> lowUp_;

    double sampleRate_  = 0.0;
    int    maxBlock_    = 0;
    int    totalLatency_ = 0;
    bool   learning_   = false;

    // --- Internals ----------------------------------------------------
    void pushLowFifo(float v) noexcept
    {
        lowFifo_[static_cast<size_t>(lowFifoWrite_)] = v;
        lowFifoWrite_ = (lowFifoWrite_ + 1) & (lowFifoSize_ - 1);
    }

    float popLowFifo() noexcept
    {
        if (lowFifoRead_ == lowFifoWrite_) return 0.0f; // empty
        const float v = lowFifo_[static_cast<size_t>(lowFifoRead_)];
        lowFifoRead_ = (lowFifoRead_ + 1) & (lowFifoSize_ - 1);
        return v;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultiBandGate)
};
