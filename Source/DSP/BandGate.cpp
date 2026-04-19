#include "DSP/BandGate.h"
#include <algorithm>
#include <cmath>

BandGate::BandGate(int fftOrder)
    : fftOrder_(clampOrder(fftOrder)),
      fftSize_ (1 << fftOrder_),
      hopSize_ (fftSize_ / 4),
      numBins_ (fftSize_ / 2 + 1),
      fftMask_ (fftSize_ - 1),
      fft_     (fftOrder_)
{
}

void BandGate::prepare(double sampleRate, int /*maxBlockSize*/)
{
    sampleRate_ = sampleRate;

    // Hann window. Same layout as SpectralGateTiptoe; pre-compute in
    // prepare() so processFrame() just indexes it.
    window_.resize(static_cast<size_t>(fftSize_));
    for (int i = 0; i < fftSize_; ++i)
        window_[static_cast<size_t>(i)] =
            0.5f * (1.0f - std::cos(2.0f * juce::MathConstants<float>::pi
                                    * static_cast<float>(i)
                                    / static_cast<float>(fftSize_)));

    // Hann at 75 % overlap sums to 2.0 — invert once so the overlap-add
    // is a multiply not a divide.
    invWindowSum_ = 0.5f;

    inputFifo_   .assign(static_cast<size_t>(fftSize_), 0.0f);
    outputFifo_  .assign(static_cast<size_t>(fftSize_), 0.0f);
    inputFifoIndex_ = 0;
    hopCounter_     = 0;

    fftWorkspace_.assign(static_cast<size_t>(fftSize_ * 2), 0.0f);

    noiseProfile_         .assign(static_cast<size_t>(numBins_), 0.0f);
    noiseProfileSq_       .assign(static_cast<size_t>(numBins_), 0.0f);
    noiseAccumulator_     .assign(static_cast<size_t>(numBins_), 0.0);
    noiseMagSqAccumulator_.assign(static_cast<size_t>(numBins_), 0.0);
    noiseFrameCount_      = 0;
    learning_             = false;
    hasNoiseProfile_      = false;

    learnFifo_   .assign(static_cast<size_t>(fftSize_), 0.0f);
    learnFifoIndex_ = 0;

    sensitivitySmoothed_  .reset(sampleRate, kParamRampSeconds);
    reductionGainSmoothed_.reset(sampleRate, kParamRampSeconds);
    sensitivitySmoothed_  .setCurrentAndTargetValue(sensitivityMultiplier_);
    reductionGainSmoothed_.setCurrentAndTargetValue(reductionGain_);

    gainState_            .assign(static_cast<size_t>(numBins_), 1.0f);
    attackMs_             .assign(static_cast<size_t>(numBins_), kDefaultAttackMs);
    releaseMs_            .assign(static_cast<size_t>(numBins_), kDefaultReleaseMs);
    attackCoef_           .resize(static_cast<size_t>(numBins_));
    releaseCoef_          .resize(static_cast<size_t>(numBins_));
    refreshTemporalCoefs();
    volatility_           .assign(static_cast<size_t>(numBins_), 0.0f);
    gainSmoothScratch_    .assign(static_cast<size_t>(numBins_), 1.0f);
    spectralSmoothScratch_.assign(static_cast<size_t>(numBins_), 1.0f);
}

void BandGate::reset()
{
    std::fill(inputFifo_ .begin(), inputFifo_ .end(), 0.0f);
    std::fill(outputFifo_.begin(), outputFifo_.end(), 0.0f);
    inputFifoIndex_ = 0;
    hopCounter_     = 0;

    std::fill(noiseProfile_        .begin(), noiseProfile_        .end(), 0.0f);
    std::fill(noiseProfileSq_      .begin(), noiseProfileSq_      .end(), 0.0f);
    std::fill(noiseAccumulator_    .begin(), noiseAccumulator_    .end(), 0.0);
    std::fill(noiseMagSqAccumulator_.begin(), noiseMagSqAccumulator_.end(), 0.0);
    noiseFrameCount_ = 0;
    learning_        = false;
    hasNoiseProfile_ = false;

    std::fill(learnFifo_.begin(), learnFifo_.end(), 0.0f);
    learnFifoIndex_ = 0;

    sensitivitySmoothed_  .setCurrentAndTargetValue(sensitivityMultiplier_);
    reductionGainSmoothed_.setCurrentAndTargetValue(reductionGain_);

    std::fill(gainState_ .begin(), gainState_ .end(), 1.0f);
    std::fill(attackMs_  .begin(), attackMs_  .end(), kDefaultAttackMs);
    std::fill(releaseMs_ .begin(), releaseMs_ .end(), kDefaultReleaseMs);
    refreshTemporalCoefs();
    std::fill(volatility_.begin(), volatility_.end(), 0.0f);
}

void BandGate::startLearning()
{
    learning_ = true;
    std::fill(noiseAccumulator_     .begin(), noiseAccumulator_     .end(), 0.0);
    std::fill(noiseMagSqAccumulator_.begin(), noiseMagSqAccumulator_.end(), 0.0);
    noiseFrameCount_ = 0;
    learnFifoIndex_  = 0;
}

void BandGate::stopLearning()
{
    learning_        = false;
    hasNoiseProfile_ = false;

    if (noiseFrameCount_ > 0)
    {
        const double invN = 1.0 / static_cast<double>(noiseFrameCount_);
        for (int i = 0; i < numBins_; ++i)
        {
            const double mean   = noiseAccumulator_[static_cast<size_t>(i)] * invN;
            const double meanSq = noiseMagSqAccumulator_[static_cast<size_t>(i)] * invN;
            const double var    = std::max(0.0, meanSq - mean * mean);
            const double stddev = std::sqrt(var);

            noiseProfile_  [static_cast<size_t>(i)] = static_cast<float>(mean);
            noiseProfileSq_[static_cast<size_t>(i)] = noiseProfile_[static_cast<size_t>(i)]
                                                   * noiseProfile_[static_cast<size_t>(i)];

            const double denom = std::max(mean, 1e-6);
            volatility_[static_cast<size_t>(i)] = static_cast<float>(stddev / denom);

            // Map volatility to per-bin time constants: volatile bins
            // get shorter attack/release so the gate tracks bursty noise;
            // stable bins get longer ones for smoother output.
            const float cv     = std::min(volatility_[static_cast<size_t>(i)], 1.0f);
            const float shrink = 1.0f - 2.0f * cv / 3.0f;
            attackMs_ [static_cast<size_t>(i)] = kDefaultAttackMs
                                                 * std::max(shrink, 1.0f / 3.0f);
            releaseMs_[static_cast<size_t>(i)] = kDefaultReleaseMs
                                                 * std::max(shrink, 1.0f / 3.0f);

            if (noiseProfile_[static_cast<size_t>(i)] > 0.0f)
                hasNoiseProfile_ = true;
        }
        refreshTemporalCoefs();
    }
    else
    {
        std::fill(noiseProfile_  .begin(), noiseProfile_  .end(), 0.0f);
        std::fill(noiseProfileSq_.begin(), noiseProfileSq_.end(), 0.0f);
        std::fill(volatility_    .begin(), volatility_    .end(), 0.0f);
    }
}

void BandGate::learnFromBlock(const float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        learnFifo_[static_cast<size_t>(learnFifoIndex_)] = samples[i];
        ++learnFifoIndex_;

        if (learnFifoIndex_ >= fftSize_)
        {
            learnFrame(learnFifo_.data());
            // Shift: keep the tail (fftSize_ - hopSize_) and leave room
            // for the next hopSize_ samples.
            std::copy(learnFifo_.begin() + hopSize_,
                      learnFifo_.end(),
                      learnFifo_.begin());
            learnFifoIndex_ = fftSize_ - hopSize_;
        }
    }
}

void BandGate::learnFrame(const float* frameData)
{
    for (int i = 0; i < fftSize_; ++i)
        fftWorkspace_[static_cast<size_t>(i)] =
            frameData[i] * window_[static_cast<size_t>(i)];

    std::fill(fftWorkspace_.begin() + fftSize_, fftWorkspace_.end(), 0.0f);

    fft_.performRealOnlyForwardTransform(fftWorkspace_.data());

    for (int i = 0; i < numBins_; ++i)
    {
        const float re    = fftWorkspace_[static_cast<size_t>(i * 2)];
        const float im    = fftWorkspace_[static_cast<size_t>(i * 2 + 1)];
        const float magSq = re * re + im * im;
        const float mag   = std::sqrt(magSq);
        noiseAccumulator_     [static_cast<size_t>(i)] += static_cast<double>(mag);
        noiseMagSqAccumulator_[static_cast<size_t>(i)] += static_cast<double>(magSq);
    }
    ++noiseFrameCount_;
}

void BandGate::setSensitivity(float sensitivityMultiplier)
{
    sensitivityMultiplier_ = sensitivityMultiplier;
    sensitivitySmoothed_.setTargetValue(sensitivityMultiplier);
}

void BandGate::setReduction(float reductionDB)
{
    reductionGain_ = std::pow(10.0f, reductionDB / 20.0f);
    reductionGainSmoothed_.setTargetValue(reductionGain_);
}

void BandGate::processMono(float* samples, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        inputFifo_[static_cast<size_t>(inputFifoIndex_)] = samples[i];

        samples[i] = outputFifo_[static_cast<size_t>(inputFifoIndex_)];
        outputFifo_[static_cast<size_t>(inputFifoIndex_)] = 0.0f;

        ++inputFifoIndex_;
        ++hopCounter_;

        if (hopCounter_ >= hopSize_)
        {
            hopCounter_ = 0;
            processFrame();
        }

        inputFifoIndex_ &= fftMask_;
    }
}

void BandGate::processFrame()
{
    // Gather + window ---------------------------------------------------
    for (int i = 0; i < fftSize_; ++i)
    {
        const int idx = (inputFifoIndex_ + i) & fftMask_;
        fftWorkspace_[static_cast<size_t>(i)] =
            inputFifo_[static_cast<size_t>(idx)] * window_[static_cast<size_t>(i)];
    }
    std::fill(fftWorkspace_.begin() + fftSize_, fftWorkspace_.end(), 0.0f);

    fft_.performRealOnlyForwardTransform(fftWorkspace_.data());

    // Advance parameter smoothers by one hop's worth of samples.
    const float effSensitivity    = sensitivitySmoothed_.skip(hopSize_);
    const float effSensitivitySq  = effSensitivity * effSensitivity;
    const float effReductionGain  = reductionGainSmoothed_.skip(hopSize_);

    // Gate pipeline -----------------------------------------------------
    if (hasNoiseProfile_)
    {
        const float overSubSq = kOverSubtractionFactor * kOverSubtractionFactor;

        // Pass 1: soft-knee target per bin.
        for (int i = 0; i < numBins_; ++i)
        {
            const float re    = fftWorkspace_[static_cast<size_t>(i * 2)];
            const float im    = fftWorkspace_[static_cast<size_t>(i * 2 + 1)];
            const float magSq = re * re + im * im;
            const float thresholdSq = noiseProfileSq_[static_cast<size_t>(i)]
                                       * effSensitivitySq * overSubSq;
            gainSmoothScratch_[static_cast<size_t>(i)] =
                softKneeGain(magSq, thresholdSq, effReductionGain);
        }

        // Pass 2: 3-bin box filter (spectral smoothing).
        for (int i = 0; i < numBins_; ++i)
        {
            float sum = gainSmoothScratch_[static_cast<size_t>(i)];
            int   count = 1;
            for (int k = 1; k <= kSpectralSmoothHalfWidth; ++k)
            {
                if (i - k >= 0)
                {
                    sum += gainSmoothScratch_[static_cast<size_t>(i - k)];
                    ++count;
                }
                if (i + k < numBins_)
                {
                    sum += gainSmoothScratch_[static_cast<size_t>(i + k)];
                    ++count;
                }
            }
            spectralSmoothScratch_[static_cast<size_t>(i)] = sum / static_cast<float>(count);
        }

        // Pass 3: per-bin temporal smoothing + apply gain.
        for (int i = 0; i < numBins_; ++i)
        {
            const float target = spectralSmoothScratch_[static_cast<size_t>(i)];
            const float coef   = (target < gainState_[static_cast<size_t>(i)])
                                     ? attackCoef_ [static_cast<size_t>(i)]
                                     : releaseCoef_[static_cast<size_t>(i)];
            gainState_[static_cast<size_t>(i)] +=
                (target - gainState_[static_cast<size_t>(i)]) * coef;

            fftWorkspace_[static_cast<size_t>(i * 2)]     *= gainState_[static_cast<size_t>(i)];
            fftWorkspace_[static_cast<size_t>(i * 2 + 1)] *= gainState_[static_cast<size_t>(i)];
        }
    }

    fft_.performRealOnlyInverseTransform(fftWorkspace_.data());

    // Overlap-add with COLA-compensating scale.
    for (int i = 0; i < fftSize_; ++i)
    {
        const int idx = (inputFifoIndex_ + i) & fftMask_;
        outputFifo_[static_cast<size_t>(idx)] +=
            fftWorkspace_[static_cast<size_t>(i)] * invWindowSum_;
    }
}

// --- Helpers ------------------------------------------------------------

float BandGate::softKneeGain(float magSq, float thresholdMagSq, float reductionGain)
{
    const float kneeLower = thresholdMagSq / kSoftKneeHalfWidthSq;
    const float kneeUpper = thresholdMagSq * kSoftKneeHalfWidthSq;

    if (magSq <= kneeLower) return reductionGain;
    if (magSq >= kneeUpper) return 1.0f;

    const float logSpan = std::log(kSoftKneeHalfWidthSq * kSoftKneeHalfWidthSq);
    float t = std::log(magSq / kneeLower) / logSpan;
    t = t * t * (3.0f - 2.0f * t); // smoothstep
    return reductionGain + t * (1.0f - reductionGain);
}

float BandGate::msToHopCoef(float ms) const
{
    if (sampleRate_ <= 0.0 || ms <= 0.0f) return 1.0f;
    const double tcSamples = static_cast<double>(ms) * sampleRate_ * 0.001;
    return 1.0f - static_cast<float>(
        std::exp(-static_cast<double>(hopSize_) / tcSamples));
}

void BandGate::refreshTemporalCoefs()
{
    for (int i = 0; i < numBins_; ++i)
    {
        attackCoef_ [static_cast<size_t>(i)] = msToHopCoef(attackMs_ [static_cast<size_t>(i)]);
        releaseCoef_[static_cast<size_t>(i)] = msToHopCoef(releaseMs_[static_cast<size_t>(i)]);
    }
}
