#include "DSP/MultiBandGate.h"
#include <algorithm>

MultiBandGate::MultiBandGate(Config cfg)
    : cfg_     (cfg),
      lowBand_ (cfg.lowFFTOrder),
      highBand_(cfg.highFFTOrder)
{
}

void MultiBandGate::prepare(double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    maxBlock_   = maxBlockSize;

    // Crossover runs at full rate.
    xo_.prepare(sampleRate, maxBlockSize);
    xo_.setCrossoverFrequency(cfg_.crossoverHz);

    // Decimator consumes full-rate, emits decimated.
    dec_.prepare(sampleRate, maxBlockSize, cfg_.decimationFactor);

    // Low-rate gate runs at fs / D. Max decimated block is maxBlockSize / D.
    const double fsLow = sampleRate / static_cast<double>(cfg_.decimationFactor);
    lowBand_.prepare(fsLow, juce::jmax(1, maxBlockSize / cfg_.decimationFactor));

    // Interpolator brings the low-band output back to full rate.
    up_.prepare(fsLow,
                juce::jmax(1, maxBlockSize / cfg_.decimationFactor),
                cfg_.decimationFactor);

    // High-rate gate runs at full rate.
    highBand_.prepare(sampleRate, maxBlockSize);

    // --- Latency accounting ------------------------------------------
    // Low-path latency in input-rate samples:
    //   decimator FIR GD
    // + lowBand FFT size (at decimated rate, expressed in input samples)
    // + interpolator FIR GD
    const int lowPath =
          dec_.getLatencyInputSamples()
        + lowBand_.getFFTSize() * cfg_.decimationFactor
        + up_.getLatencyInputSamples();

    // High-path latency = the high-rate FFT size at full rate.
    const int highPath = highBand_.getFFTSize();

    // Align the faster band by delaying it to the slower one's latency.
    const int delayForHigh = juce::jmax(0, lowPath - highPath);
    highDelaySize_ = delayForHigh + 1; // +1 so write/read don't overlap at 0-delay
    highDelayBuf_.assign(static_cast<size_t>(highDelaySize_), 0.0f);
    highDelayPos_ = 0;

    totalLatency_ = juce::jmax(lowPath, highPath);

    // --- Low-band output FIFO ----------------------------------------
    // Needs to hold at least a couple of blocks' worth of samples to
    // absorb the occasional D-sample lag from the decimator's sample
    // counter. Round up to the next power of two so wraparound is a
    // bitmask.
    int needed = maxBlockSize * 4;
    int fifoSize = 1;
    while (fifoSize < needed) fifoSize <<= 1;
    lowFifoSize_  = fifoSize;
    lowFifo_.assign(static_cast<size_t>(fifoSize), 0.0f);
    lowFifoWrite_ = 0;
    lowFifoRead_  = 0;

    // --- Scratch buffers ---------------------------------------------
    lowFull_ .assign(static_cast<size_t>(maxBlockSize), 0.0f);
    highFull_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    // Decimated and interpolated scratches — decimated can hold up to
    // maxBlockSize (upper bound; actual count ≤ maxBlockSize/D).
    lowDec_.assign(static_cast<size_t>(maxBlockSize), 0.0f);
    lowUp_ .assign(static_cast<size_t>(maxBlockSize), 0.0f);
}

void MultiBandGate::reset()
{
    xo_      .reset();
    dec_     .reset();
    lowBand_ .reset();
    up_      .reset();
    highBand_.reset();

    std::fill(highDelayBuf_.begin(), highDelayBuf_.end(), 0.0f);
    highDelayPos_ = 0;
    std::fill(lowFifo_.begin(), lowFifo_.end(), 0.0f);
    lowFifoWrite_ = 0;
    lowFifoRead_  = 0;
    learning_     = false;
}

void MultiBandGate::startLearning()
{
    learning_ = true;
    lowBand_ .startLearning();
    highBand_.startLearning();
}

void MultiBandGate::stopLearning()
{
    lowBand_ .stopLearning();
    highBand_.stopLearning();
    learning_ = false;
}

void MultiBandGate::learnFromBlock(const float* samples, int numSamples)
{
    // Chunk into prepare()-sized blocks. Learn buffers in the wild are
    // often many seconds long (tens of thousands of samples); the DSP
    // scratch is only sized for maxBlock_.
    int offset = 0;
    while (offset < numSamples)
    {
        const int chunk = juce::jmin(maxBlock_, numSamples - offset);

        // Route input through the crossover so each band only learns its
        // own frequency range — keeps the per-band noise profile clean.
        xo_.process(samples + offset, lowFull_.data(), highFull_.data(), chunk);

        // Feed the high band at full rate; feed the low band post-
        // decimation so its FFT sees the rate it'll process at.
        const int decCount = dec_.process(lowFull_.data(), lowDec_.data(), chunk);
        lowBand_ .learnFromBlock(lowDec_.data(),  decCount);
        highBand_.learnFromBlock(highFull_.data(), chunk);

        offset += chunk;
    }
}

void MultiBandGate::setSensitivity(float sensitivityMultiplier)
{
    lowBand_ .setSensitivity(sensitivityMultiplier);
    highBand_.setSensitivity(sensitivityMultiplier);
}

void MultiBandGate::setReduction(float reductionDB)
{
    lowBand_ .setReduction(reductionDB);
    highBand_.setReduction(reductionDB);
}

namespace
{
    // Quadrature-sum bin-by-bin (√((lowScale·low)² + high²)). The low
    // band covers the first `low.size()` bins; the high band fills the
    // full range. Both FFTs land on the same 344 Hz bin grid by design.
    //
    // The low band is FFT'd at the DECIMATED rate (fs/D) with FFT size
    // N_low such that fs_low/N_low = fs_high/N_high — same bin width.
    // But raw FFT peak magnitudes scale as N_bins × signal_amplitude,
    // so a given sine gives N_low/4 in the low FFT vs N_high/4 in the
    // high FFT. Scaling low by (N_high / N_low) puts both on the same
    // amplitude grid so the merged spectrum matches the HQ-mode scale
    // (SpectralGateTiptoe uses the full-rate FFT and magRef = N/4).
    inline void mergeBands(const std::vector<float>& low,
                           const std::vector<float>& high,
                           float lowScale,
                           std::vector<float>& out)
    {
        const size_t n = high.size();
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            const float h = high[i];
            const float l = i < low.size() ? (low[i] * lowScale) : 0.0f;
            out[i] = std::sqrt(h * h + l * l);
        }
    }
}

void MultiBandGate::copyInputMagnitudes(std::vector<float>& out) const
{
    static thread_local std::vector<float> lowScratch, highScratch;
    lowBand_ .copyInputMagnitudes(lowScratch);
    highBand_.copyInputMagnitudes(highScratch);
    const float lowScale = static_cast<float>(highBand_.getFFTSize())
                         / static_cast<float>(lowBand_.getFFTSize());
    mergeBands(lowScratch, highScratch, lowScale, out);
}

void MultiBandGate::copyNoiseProfile(std::vector<float>& out) const
{
    static thread_local std::vector<float> lowScratch, highScratch;
    lowBand_ .copyNoiseProfile(lowScratch);
    highBand_.copyNoiseProfile(highScratch);
    const float lowScale = static_cast<float>(highBand_.getFFTSize())
                         / static_cast<float>(lowBand_.getFFTSize());
    mergeBands(lowScratch, highScratch, lowScale, out);
}

void MultiBandGate::processMono(float* samples, int numSamples)
{
    if (numSamples <= 0) return;

    // Internal chunking: scratch buffers are sized to maxBlock_, so callers
    // passing larger blocks (e.g. offline rendering, tests) would overrun
    // them. Chunk down to maxBlock_ to keep all DSP reads/writes inside
    // their prepared sizes — without this, MSVC /GS raises fail-fast on
    // the stack/heap corruption, while macOS/clang happens to tolerate it.
    int offset = 0;
    while (offset < numSamples)
    {
        const int n = juce::jmin(maxBlock_, numSamples - offset);
        processChunk(samples + offset, n);
        offset += n;
    }
}

void MultiBandGate::processChunk(float* samples, int numSamples)
{
    // 1. Crossover split (full rate in, two full-rate outputs).
    xo_.process(samples, lowFull_.data(), highFull_.data(), numSamples);

    // 2. Low path: decimate → low gate → interpolate.
    //    Decimator consumes numSamples, produces `decCount` decimated samples.
    const int decCount = dec_.process(lowFull_.data(), lowDec_.data(), numSamples);
    lowBand_.processMono(lowDec_.data(), decCount);
    const int upCount  = up_.process(lowDec_.data(), lowUp_.data(), decCount);

    // 3. Push interpolated low-band samples into the FIFO. Steady state:
    //    upCount == numSamples each call; startup / uneven blocks leave
    //    the FIFO with a small surplus or deficit that smooths out.
    for (int i = 0; i < upCount; ++i)
        pushLowFifo(lowUp_[static_cast<size_t>(i)]);

    // 4. High path: process at full rate.
    highBand_.processMono(highFull_.data(), numSamples);

    // 5. Combine — pop low-band samples + delay high-band to match.
    for (int i = 0; i < numSamples; ++i)
    {
        const float low        = popLowFifo();
        // Insert current high sample, read out the delayed one.
        const float delayedHi  = highDelayBuf_[static_cast<size_t>(highDelayPos_)];
        highDelayBuf_[static_cast<size_t>(highDelayPos_)] = highFull_[static_cast<size_t>(i)];
        highDelayPos_ = (highDelayPos_ + 1) % highDelaySize_;

        samples[i] = low + delayedHi;
    }
}
