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

    // Parameter smoothers: ramp length measured in audio samples, advanced
    // one hop per processFrame() so the 30 ms figure is in wall-clock time.
    thresholdSmoothed_.reset(sampleRate, kParamRampSeconds);
    reductionGainSmoothed_.reset(sampleRate, kParamRampSeconds);
    thresholdSmoothed_.setCurrentAndTargetValue(thresholdMultiplier_);
    reductionGainSmoothed_.setCurrentAndTargetValue(reductionGain_);

    // Artifact-reduction state.
    gainState_.assign(kNumBins, 1.0f);
    attackMs_.assign(kNumBins, kDefaultAttackMs);
    releaseMs_.assign(kNumBins, kDefaultReleaseMs);
    attackCoef_.resize(kNumBins);
    releaseCoef_.resize(kNumBins);
    refreshTemporalCoefs();
    noiseMagSqAccumulator_.assign(kNumBins, 0.0);
    volatility_.assign(kNumBins, 0.0f);
    gainSmoothScratch_.assign(kNumBins, 1.0f);
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

    thresholdSmoothed_.setCurrentAndTargetValue(thresholdMultiplier_);
    reductionGainSmoothed_.setCurrentAndTargetValue(reductionGain_);

    std::fill(gainState_.begin(), gainState_.end(), 1.0f);
    std::fill(attackMs_.begin(), attackMs_.end(), kDefaultAttackMs);
    std::fill(releaseMs_.begin(), releaseMs_.end(), kDefaultReleaseMs);
    refreshTemporalCoefs();
    std::fill(noiseMagSqAccumulator_.begin(), noiseMagSqAccumulator_.end(), 0.0);
    std::fill(volatility_.begin(), volatility_.end(), 0.0f);
}

void SpectralGateTiptoe::startLearning()
{
    learning_ = true;
    std::fill(noiseAccumulator_.begin(), noiseAccumulator_.end(), 0.0);
    std::fill(noiseMagSqAccumulator_.begin(), noiseMagSqAccumulator_.end(), 0.0);
    noiseFrameCount_ = 0;
    learnFifoIndex_ = 0;
}

void SpectralGateTiptoe::stopLearning()
{
    learning_ = false;
    hasNoiseProfile_ = false;

    if (noiseFrameCount_ > 0)
    {
        const double invN = 1.0 / static_cast<double>(noiseFrameCount_);
        for (int i = 0; i < kNumBins; ++i)
        {
            const double mean   = noiseAccumulator_[i] * invN;
            const double meanSq = noiseMagSqAccumulator_[i] * invN;
            const double var    = std::max(0.0, meanSq - mean * mean);
            const double stddev = std::sqrt(var);

            noiseProfile_[i]   = static_cast<float>(mean);
            noiseProfileSq_[i] = noiseProfile_[i] * noiseProfile_[i];

            // Coefficient of variation — stddev normalised by mean. Robust
            // against bins with very small noise (guarded denominator).
            const double denom = std::max(mean, 1e-6);
            volatility_[i] = static_cast<float>(stddev / denom);

            // Map CV -> time constants. Volatile bins get shorter times so
            // the gate can react to bursty noise; stable bins get longer
            // times for smoother gating. Lerp between default and 1/3 of it.
            const float cv     = std::min(volatility_[i], 1.0f);
            const float shrink = 1.0f - 2.0f * cv / 3.0f;
            attackMs_[i]  = kDefaultAttackMs  * std::max(shrink, 1.0f / 3.0f);
            releaseMs_[i] = kDefaultReleaseMs * std::max(shrink, 1.0f / 3.0f);

            if (noiseProfile_[i] > 0.0f)
                hasNoiseProfile_ = true;
        }
        refreshTemporalCoefs();
    }
    else
    {
        std::fill(noiseProfile_.begin(), noiseProfile_.end(), 0.0f);
        std::fill(noiseProfileSq_.begin(), noiseProfileSq_.end(), 0.0f);
        std::fill(volatility_.begin(), volatility_.end(), 0.0f);
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
        const float magSq = re * re + im * im;
        const float mag = std::sqrt(magSq);
        inputDst[static_cast<size_t>(i)] = mag;
        noiseAccumulator_[i]      += static_cast<double>(mag);
        noiseMagSqAccumulator_[i] += static_cast<double>(magSq);
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
    thresholdSmoothed_.setTargetValue(thresholdMultiplier);
}

void SpectralGateTiptoe::setReduction(float reductionDB)
{
    reductionGain_ = std::pow(10.0f, reductionDB / 20.0f);
    reductionGainSmoothed_.setTargetValue(reductionGain_);
}

float SpectralGateTiptoe::getEffectiveThresholdMultiplier() const
{
    return thresholdSmoothed_.getCurrentValue();
}

float SpectralGateTiptoe::getEffectiveReductionGain() const
{
    return reductionGainSmoothed_.getCurrentValue();
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

    // Advance parameter smoothers by one hop's worth of audio samples.
    // Reading the smoothed values here (rather than the raw thresholdSq_ /
    // reductionGain_) is what makes automation glide instead of stepping
    // at the FFT hop rate.
    const float effThresholdMult  = thresholdSmoothed_.skip(kHopSize);
    const float effThresholdSq    = effThresholdMult * effThresholdMult;
    const float effReductionGain  = reductionGainSmoothed_.skip(kHopSize);

    // Spectral gate — soft-knee per-bin gain with spectral smoothing across
    // neighbours and temporal smoothing per bin. The effective threshold
    // is multiplied by an over-subtraction factor to fight residual musical
    // noise from variance in the noise floor.
    if (hasNoiseProfile_)
    {
        const float overSubSq = kOverSubtractionFactor * kOverSubtractionFactor;

        // Pass 1: target gain per bin from soft-knee against the threshold.
        for (int i = 0; i < kNumBins; ++i)
        {
            const float re = fftWorkspace[i * 2];
            const float im = fftWorkspace[i * 2 + 1];
            const float magSq = re * re + im * im;

            const float thresholdSq = noiseProfileSq_[i] * effThresholdSq * overSubSq;
            gainSmoothScratch_[i] = softKneeGain(magSq, thresholdSq, effReductionGain);
        }

        // Pass 2: spectral smoothing — 3-bin box filter of the target gains
        // so isolated bins don't get singled out as "not noise" or "noise"
        // when their neighbours disagree.
        std::vector<float> smoothedTargets(kNumBins);
        for (int i = 0; i < kNumBins; ++i)
        {
            float sum = gainSmoothScratch_[i];
            int count = 1;
            for (int k = 1; k <= kSpectralSmoothHalfWidth; ++k)
            {
                if (i - k >= 0)                { sum += gainSmoothScratch_[i - k]; ++count; }
                if (i + k < kNumBins)          { sum += gainSmoothScratch_[i + k]; ++count; }
            }
            smoothedTargets[i] = sum / static_cast<float>(count);
        }

        // Pass 3: per-bin temporal smoothing toward the smoothed target
        // with asymmetric attack / release, then apply final gain.
        for (int i = 0; i < kNumBins; ++i)
        {
            const float target = smoothedTargets[i];
            const float coef   = (target < gainState_[i]) ? attackCoef_[i]
                                                          : releaseCoef_[i];
            gainState_[i] += (target - gainState_[i]) * coef;

            fftWorkspace[i * 2]     *= gainState_[i];
            fftWorkspace[i * 2 + 1] *= gainState_[i];
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

// ─────────── Artifact-reduction helpers ───────────

float SpectralGateTiptoe::softKneeGain(float magSq,
                                       float thresholdMagSq,
                                       float reductionGain)
{
    // Log-symmetric soft knee: at magSq == thresholdMagSq (dB delta = 0)
    // we're exactly halfway through the knee. kSoftKneeHalfWidthSq = 4
    // corresponds to ± 6 dB in power around the threshold.
    const float kneeLower = thresholdMagSq / kSoftKneeHalfWidthSq;
    const float kneeUpper = thresholdMagSq * kSoftKneeHalfWidthSq;

    if (magSq <= kneeLower) return reductionGain;
    if (magSq >= kneeUpper) return 1.0f;

    // Position in log space: t = log(magSq/kneeLower) / log(kneeUpper/kneeLower)
    //                         = log(magSq/kneeLower) / log(kSoftKneeHalfWidthSq²)
    const float logSpan = std::log(kSoftKneeHalfWidthSq * kSoftKneeHalfWidthSq);
    float t = std::log(magSq / kneeLower) / logSpan;
    t = t * t * (3.0f - 2.0f * t); // smoothstep
    return reductionGain + t * (1.0f - reductionGain);
}

float SpectralGateTiptoe::msToHopCoef(float ms) const
{
    if (sampleRate_ <= 0.0 || ms <= 0.0f)
        return 1.0f;
    const double tcSamples = static_cast<double>(ms) * sampleRate_ * 0.001;
    return 1.0f - static_cast<float>(std::exp(-static_cast<double>(kHopSize) / tcSamples));
}

void SpectralGateTiptoe::refreshTemporalCoefs()
{
    attackCoef_.resize(kNumBins);
    releaseCoef_.resize(kNumBins);
    for (int i = 0; i < kNumBins; ++i)
    {
        attackCoef_[i]  = msToHopCoef(attackMs_[i]);
        releaseCoef_[i] = msToHopCoef(releaseMs_[i]);
    }
}

float SpectralGateTiptoe::getBinGainState(int bin) const
{
    if (bin < 0 || bin >= static_cast<int>(gainState_.size())) return 1.0f;
    return gainState_[static_cast<size_t>(bin)];
}

float SpectralGateTiptoe::getBinVolatility(int bin) const
{
    if (bin < 0 || bin >= static_cast<int>(volatility_.size())) return 0.0f;
    return volatility_[static_cast<size_t>(bin)];
}

float SpectralGateTiptoe::getBinAttackMs(int bin) const
{
    if (bin < 0 || bin >= static_cast<int>(attackMs_.size())) return kDefaultAttackMs;
    return attackMs_[static_cast<size_t>(bin)];
}

float SpectralGateTiptoe::getBinReleaseMs(int bin) const
{
    if (bin < 0 || bin >= static_cast<int>(releaseMs_.size())) return kDefaultReleaseMs;
    return releaseMs_[static_cast<size_t>(bin)];
}
