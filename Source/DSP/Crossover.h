#pragma once

#include <juce_dsp/juce_dsp.h>

// Two-band Linkwitz-Riley crossover: splits an input mono stream into a
// lowpass band and a highpass band whose magnitude sum is flat across the
// spectrum. Built on juce::dsp::LinkwitzRileyFilter so we get 4th-order
// Butterworth-squared slopes (-24 dB/oct) and a phase-coherent sum for
// free.
//
// Zero heap allocation in process() — two stack filters, in-place biquad
// cascades. The allpass-sum of the two LR filters reconstructs unity
// magnitude to within rounding error (see TestCrossover).
//
// Usage:
//   Crossover xo;
//   xo.prepare(sampleRate, maxBlockSize);
//   xo.setCrossoverFrequency(2000.0f);
//   xo.process(in, low, high, numSamples);
class Crossover
{
public:
    Crossover() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sampleRate_ = sampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, maxBlockSize));
        spec.numChannels      = 1; // mono per band — stereo is driven by the caller

        lp_.prepare(spec);
        hp_.prepare(spec);
        lp_.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
        hp_.setType(juce::dsp::LinkwitzRileyFilterType::highpass);

        // Default crossover — callers almost always override via
        // setCrossoverFrequency(), but having a sane default means an
        // un-set filter still produces valid output.
        setCrossoverFrequency(2000.0f);

        reset();
    }

    void reset() noexcept
    {
        lp_.reset();
        hp_.reset();
    }

    // Clamps to [20 Hz, Nyquist − 100 Hz]. DC and Nyquist are invalid
    // cutoffs for biquad stability.
    void setCrossoverFrequency(float freqHz) noexcept
    {
        if (! std::isfinite(freqHz))
            freqHz = 2000.0f;
        const float maxCutoff = static_cast<float>(sampleRate_) * 0.5f - 100.0f;
        freqHz = juce::jlimit(20.0f, juce::jmax(100.0f, maxCutoff), freqHz);
        lp_.setCutoffFrequency(freqHz);
        hp_.setCutoffFrequency(freqHz);
    }

    // Mono split. `in`, `low`, `high` may alias (low == in is safe; high
    // must be distinct from in). Most callers provide three distinct
    // buffers, so the common path is no-alias.
    void process(const float* in, float* low, float* high, int numSamples) noexcept
    {
        // LinkwitzRileyFilter processes samples in-place over AudioBlocks.
        // We could allocate a juce::AudioBuffer here, but we want zero
        // heap traffic on the audio thread — sample-by-sample keeps the
        // call site cache-friendly and the biquad states hot in L1.
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = in[i];
            low [i] = lp_.processSample(0, x);
            high[i] = hp_.processSample(0, x);
        }
    }

private:
    double sampleRate_ = 44100.0;
    juce::dsp::LinkwitzRileyFilter<float> lp_ {};
    juce::dsp::LinkwitzRileyFilter<float> hp_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Crossover)
};
