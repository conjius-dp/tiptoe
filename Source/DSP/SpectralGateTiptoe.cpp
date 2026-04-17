#include "DSP/SpectralGateTiptoe.h"
#include <chrono>
#include <cmath>
#include <algorithm>

SpectralGateTiptoe::SpectralGateTiptoe()
    : fft(kFFTOrder)
{
}

void SpectralGateTiptoe::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Hann window
    window.resize(kFFTSize);
    for (int i = 0; i < kFFTSize; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi * static_cast<float>(i) / static_cast<float>(kFFTSize)));

    // With 50% overlap and Hann window on analysis only, COLA sum = 1.0
    // No normalization needed (invWindowSum_ = 1.0)
    invWindowSum_ = 1.0f;

    inputFifo.resize(kFFTSize, 0.0f);
    outputFifo.resize(kFFTSize, 0.0f);
    inputFifoIndex = 0;
    hopCounter = 0;

    fftWorkspace.resize(kFFTSize * 2, 0.0f);

    noiseProfile_.resize(kNumBins, 0.0f);
    noiseProfileSq_.resize(kNumBins, 0.0f);
    noiseAccumulator_.resize(kNumBins, 0.0);
    noiseFrameCount_ = 0;
    learning_ = false;
    hasNoiseProfile_ = false;

    learnFifo_.resize(kFFTSize, 0.0f);
    learnFifoIndex_ = 0;

    for (auto& buf : inputMagSnapshots_)
        buf.assign(kNumBins, 0.0f);
    inputMagWriteIndex_.store(0, std::memory_order_relaxed);

    for (auto& buf : noiseSnapshots_)
        buf.assign(kNumBins, 0.0f);
    noiseWriteIndex_.store(0, std::memory_order_relaxed);
}

void SpectralGateTiptoe::reset()
{
    std::fill(inputFifo.begin(), inputFifo.end(), 0.0f);
    std::fill(outputFifo.begin(), outputFifo.end(), 0.0f);
    inputFifoIndex = 0;
    hopCounter = 0;

    std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
    std::fill(noiseProfileSq_.begin(), noiseProfileSq_.end(), 0.0f);
    std::fill(noiseAccumulator_.begin(), noiseAccumulator_.end(), 0.0);
    noiseFrameCount_ = 0;
    learning_ = false;
    hasNoiseProfile_ = false;

    std::fill(learnFifo_.begin(), learnFifo_.end(), 0.0f);
    learnFifoIndex_ = 0;
}

void SpectralGateTiptoe::startLearning()
{
    learning_ = true;
    std::fill(noiseAccumulator_.begin(), noiseAccumulator_.end(), 0.0);
    noiseFrameCount_ = 0;
    learnFifoIndex_ = 0;
}

void SpectralGateTiptoe::stopLearning()
{
    learning_ = false;
    hasNoiseProfile_ = false;

    if (noiseFrameCount_ > 0)
    {
        for (int i = 0; i < kNumBins; ++i)
        {
            noiseProfile_[i] = static_cast<float>(noiseAccumulator_[i] / static_cast<double>(noiseFrameCount_));
            noiseProfileSq_[i] = noiseProfile_[i] * noiseProfile_[i];

            if (noiseProfile_[i] > 0.0f)
                hasNoiseProfile_ = true;
        }
    }
    else
    {
        std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
        std::fill(noiseProfileSq_.begin(), noiseProfileSq_.end(), 0.0f);
    }

    // Publish the final averaged profile as the visible snapshot.
    const int writeIdx = (noiseWriteIndex_.load(std::memory_order_relaxed) + 1) & 1;
    noiseSnapshots_[static_cast<size_t>(writeIdx)] = noiseProfile_;
    noiseWriteIndex_.store(writeIdx, std::memory_order_release);
}

bool SpectralGateTiptoe::isLearning() const
{
    return learning_;
}

void SpectralGateTiptoe::learnFromBlock(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        learnFifo_[learnFifoIndex_] = samples[i];
        ++learnFifoIndex_;

        if (learnFifoIndex_ >= kFFTSize)
        {
            learnFrame(learnFifo_.data());
            std::copy(learnFifo_.begin() + kHopSize, learnFifo_.end(), learnFifo_.begin());
            learnFifoIndex_ = kFFTSize - kHopSize;
        }
    }
}

void SpectralGateTiptoe::learnFrame(const float* frameData)
{
    for (int i = 0; i < kFFTSize; ++i)
        fftWorkspace[i] = frameData[i] * window[i];

    std::fill(fftWorkspace.begin() + kFFTSize, fftWorkspace.end(), 0.0f);

    fft.performRealOnlyForwardTransform(fftWorkspace.data());

    // Publish per-bin magnitudes of THIS frame so the UI keeps seeing a live
    // input curve during learning (processFrame() is never hit while learning
    // because the processor short-circuits).
    const int inputWriteIdx = (inputMagWriteIndex_.load(std::memory_order_relaxed) + 1) & 1;
    auto& inputDst = inputMagSnapshots_[static_cast<size_t>(inputWriteIdx)];

    for (int i = 0; i < kNumBins; ++i)
    {
        const float re = fftWorkspace[i * 2];
        const float im = fftWorkspace[i * 2 + 1];
        const float mag = std::sqrt(re * re + im * im);
        inputDst[static_cast<size_t>(i)] = mag;
        noiseAccumulator_[i] += static_cast<double>(mag);
    }
    inputMagWriteIndex_.store(inputWriteIdx, std::memory_order_release);
    ++noiseFrameCount_;

    // Publish the running-average noise profile so the noise curve morphs
    // visibly while the user is learning.
    if (noiseFrameCount_ > 0)
    {
        const int noiseWriteIdx = (noiseWriteIndex_.load(std::memory_order_relaxed) + 1) & 1;
        auto& noiseDst = noiseSnapshots_[static_cast<size_t>(noiseWriteIdx)];
        const double inv = 1.0 / static_cast<double>(noiseFrameCount_);
        for (int i = 0; i < kNumBins; ++i)
            noiseDst[static_cast<size_t>(i)] = static_cast<float>(noiseAccumulator_[i] * inv);
        noiseWriteIndex_.store(noiseWriteIdx, std::memory_order_release);
    }
}

const std::vector<float>& SpectralGateTiptoe::getNoiseProfile() const
{
    return noiseProfile_;
}

void SpectralGateTiptoe::setThreshold(float thresholdMultiplier)
{
    thresholdMultiplier_ = thresholdMultiplier;
    thresholdSq_ = thresholdMultiplier * thresholdMultiplier;
}

void SpectralGateTiptoe::setReduction(float reductionDB)
{
    reductionGain_ = std::pow(10.0f, reductionDB / 20.0f);
}

float SpectralGateTiptoe::getLastProcessingTimeMs() const
{
    return lastProcessingTimeMs_;
}

void SpectralGateTiptoe::copyInputMagnitudes(std::vector<float>& out) const
{
    const int readIdx = inputMagWriteIndex_.load(std::memory_order_acquire);
    const auto& src = inputMagSnapshots_[static_cast<size_t>(readIdx)];
    out.resize(src.size());
    std::copy(src.begin(), src.end(), out.begin());
}

void SpectralGateTiptoe::copyNoiseProfile(std::vector<float>& out) const
{
    const int readIdx = noiseWriteIndex_.load(std::memory_order_acquire);
    const auto& src = noiseSnapshots_[static_cast<size_t>(readIdx)];
    out.resize(src.size());
    std::copy(src.begin(), src.end(), out.begin());
}

void SpectralGateTiptoe::processMono(float* samples, int numSamples)
{
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numSamples; ++i)
    {
        inputFifo[inputFifoIndex] = samples[i];

        samples[i] = outputFifo[inputFifoIndex];
        outputFifo[inputFifoIndex] = 0.0f;

        ++inputFifoIndex;
        ++hopCounter;

        if (hopCounter >= kHopSize)
        {
            hopCounter = 0;
            processFrame();
        }

        inputFifoIndex &= kFFTMask; // bitwise AND instead of modulo
    }

    auto end = std::chrono::high_resolution_clock::now();
    lastProcessingTimeMs_ = std::chrono::duration<float, std::milli>(end - start).count();
}

void SpectralGateTiptoe::processFrame()
{
    // Gather and window using bitwise AND for wrap
    for (int i = 0; i < kFFTSize; ++i)
    {
        int idx = (inputFifoIndex + i) & kFFTMask;
        fftWorkspace[i] = inputFifo[idx] * window[i];
    }
    std::fill(fftWorkspace.begin() + kFFTSize, fftWorkspace.end(), 0.0f);

    fft.performRealOnlyForwardTransform(fftWorkspace.data());

    // Snapshot per-bin magnitudes into the inactive buffer before gating
    // modifies anything — then flip the write index so UI readers see it.
    {
        const int writeIdx = (inputMagWriteIndex_.load(std::memory_order_relaxed) + 1) & 1;
        auto& dst = inputMagSnapshots_[static_cast<size_t>(writeIdx)];
        for (int i = 0; i < kNumBins; ++i)
        {
            const float re = fftWorkspace[i * 2];
            const float im = fftWorkspace[i * 2 + 1];
            dst[static_cast<size_t>(i)] = std::sqrt(re * re + im * im);
        }
        inputMagWriteIndex_.store(writeIdx, std::memory_order_release);
    }

    // Spectral gate — compare squared magnitudes (no sqrt)
    if (hasNoiseProfile_)
    {
        for (int i = 0; i < kNumBins; ++i)
        {
            float re = fftWorkspace[i * 2];
            float im = fftWorkspace[i * 2 + 1];
            float magSq = re * re + im * im;

            float thresholdSq = noiseProfileSq_[i] * thresholdSq_;

            if (magSq < thresholdSq)
            {
                fftWorkspace[i * 2] *= reductionGain_;
                fftWorkspace[i * 2 + 1] *= reductionGain_;
            }
        }
    }

    fft.performRealOnlyInverseTransform(fftWorkspace.data());

    // Overlap-add — no synthesis window needed with Hann analysis + 50% overlap (COLA = 1.0)
    for (int i = 0; i < kFFTSize; ++i)
    {
        int idx = (inputFifoIndex + i) & kFFTMask;
        outputFifo[idx] += fftWorkspace[i];
    }
}
