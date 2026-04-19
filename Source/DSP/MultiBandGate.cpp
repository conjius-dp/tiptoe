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

void MultiBandGate::processMono(float* samples, int numSamples)
{
    // Guard against callers exceeding prepare()'s maxBlockSize.
    jassert(numSamples <= maxBlock_);
    if (numSamples <= 0) return;

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
