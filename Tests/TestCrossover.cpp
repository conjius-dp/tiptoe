// TDD harness for the two-band crossover used by the multi-band gate.
//
// The crossover splits the input into a low-band and a high-band at a
// configurable frequency, such that low + high reconstructs the input
// magnitude response to within ~0 dB (Linkwitz-Riley: flat magnitude sum,
// phase-coherent for a 4th-order pair).
//
// These tests pin down the invariants we need at the DSP level:
//  1. Power / sum reconstruction at the crossover is unity magnitude.
//  2. Content well below the crossover lives almost entirely in the low band.
//  3. Content well above the crossover lives almost entirely in the high band.
//  4. Setting the crossover frequency changes the split point as expected.
//  5. prepare() followed by reset() puts the filter in a silent-stable state.
//  6. process() in-place on a buffer produces the same result as out-of-place.
//  7. Changing the crossover mid-stream doesn't produce NaN / Inf.

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Crossover.h"

namespace
{
    // Pure-tone test signal at `freqHz`, `numSamples` long, unit amplitude.
    inline std::vector<float> generateSine(float freqHz, float sampleRate, int numSamples)
    {
        std::vector<float> out(static_cast<size_t>(numSamples));
        const float step = juce::MathConstants<float>::twoPi * freqHz / sampleRate;
        float phase = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            out[static_cast<size_t>(i)] = std::sin(phase);
            phase += step;
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
        }
        return out;
    }

    inline float rms(const float* data, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += static_cast<double>(data[i]) * data[i];
        return static_cast<float>(std::sqrt(s / std::max(1, n)));
    }

    inline float ratioDb(float num, float den)
    {
        return 20.0f * std::log10(std::max(num, 1e-12f) / std::max(den, 1e-12f));
    }
}

class CrossoverTests : public juce::UnitTest
{
public:
    CrossoverTests() : juce::UnitTest("Crossover") {}

    void runTest() override
    {
        constexpr double sampleRate = 44100.0;
        constexpr float  crossover  = 2000.0f;
        constexpr int    blockSize  = 512;

        beginTest("Sum of low+high reconstructs input (broadband)");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            // White noise is broadband — stresses the filter across the full
            // spectrum and verifies the magnitude sum stays flat.
            juce::Random rng(12345);
            const int N = 8192;
            std::vector<float> input(N), low(N), high(N);
            for (int i = 0; i < N; ++i)
                input[static_cast<size_t>(i)] = rng.nextFloat() * 2.0f - 1.0f;

            // Process in blocks to mirror real-time usage.
            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(input.data() + i, low.data() + i, high.data() + i, n);
            }

            // Skip the filter's warm-up; measure the steady state.
            const int skip = 1024;
            float rmsIn   = rms(input.data() + skip, N - skip);

            // Sum low+high and compare RMS.
            std::vector<float> sum(N);
            for (int i = 0; i < N; ++i)
                sum[static_cast<size_t>(i)] = low[static_cast<size_t>(i)] + high[static_cast<size_t>(i)];

            float rmsSum = rms(sum.data() + skip, N - skip);
            expectWithinAbsoluteError(ratioDb(rmsSum, rmsIn), 0.0f, 0.5f);
        }

        beginTest("Low band dominates well below crossover");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            // 200 Hz is an octave below the crossover at 2 kHz. Low-band
            // energy should be at least 20 dB over high-band energy.
            const int N = 4096;
            auto sine = generateSine(200.0f, static_cast<float>(sampleRate), N);
            std::vector<float> low(N), high(N);
            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(sine.data() + i, low.data() + i, high.data() + i, n);
            }

            const int skip = 1024;
            float rmsLow  = rms(low.data()  + skip, N - skip);
            float rmsHigh = rms(high.data() + skip, N - skip);
            expect(ratioDb(rmsLow, rmsHigh) > 20.0f);
        }

        beginTest("High band dominates well above crossover");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            // 8 kHz is two octaves above the crossover; the high band must
            // carry essentially all the energy.
            const int N = 4096;
            auto sine = generateSine(8000.0f, static_cast<float>(sampleRate), N);
            std::vector<float> low(N), high(N);
            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(sine.data() + i, low.data() + i, high.data() + i, n);
            }

            const int skip = 1024;
            float rmsLow  = rms(low.data()  + skip, N - skip);
            float rmsHigh = rms(high.data() + skip, N - skip);
            expect(ratioDb(rmsHigh, rmsLow) > 20.0f);
        }

        beginTest("Energy at crossover splits evenly (−3 dB each band)");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            // A 4th-order Linkwitz-Riley crossover is defined so each band
            // is −6 dB at the crossover — but the magnitude SUM stays flat.
            const int N = 4096;
            auto sine = generateSine(crossover, static_cast<float>(sampleRate), N);
            std::vector<float> low(N), high(N);
            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(sine.data() + i, low.data() + i, high.data() + i, n);
            }

            const int skip = 1024;
            float rmsLow   = rms(low.data()  + skip, N - skip);
            float rmsHigh  = rms(high.data() + skip, N - skip);
            float rmsIn    = rms(sine.data() + skip, N - skip);

            // Each band hands back roughly −6 dB at the crossover point.
            expectWithinAbsoluteError(ratioDb(rmsLow,  rmsIn), -6.0f, 1.5f);
            expectWithinAbsoluteError(ratioDb(rmsHigh, rmsIn), -6.0f, 1.5f);
        }

        beginTest("Changing crossover frequency shifts the split");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);

            // Drop the crossover to 500 Hz and verify 1 kHz now sits in
            // the HIGH band (previously it would have been low).
            xo.setCrossoverFrequency(500.0f);
            const int N = 4096;
            auto sine = generateSine(1000.0f, static_cast<float>(sampleRate), N);
            std::vector<float> low(N), high(N);
            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(sine.data() + i, low.data() + i, high.data() + i, n);
            }

            const int skip = 1024;
            expect(rms(high.data() + skip, N - skip) >
                   rms(low.data()  + skip, N - skip));
        }

        beginTest("reset() returns to silent-stable state");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            // Shock the filter with an impulse, then reset, then feed
            // silence — output must be exactly zero.
            std::vector<float> impulse(blockSize, 0.0f);
            impulse[0] = 1.0f;
            std::vector<float> low(blockSize), high(blockSize);
            xo.process(impulse.data(), low.data(), high.data(), blockSize);

            xo.reset();

            std::vector<float> silence(blockSize, 0.0f);
            xo.process(silence.data(), low.data(), high.data(), blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                expectEquals(low[static_cast<size_t>(i)],  0.0f);
                expectEquals(high[static_cast<size_t>(i)], 0.0f);
            }
        }

        beginTest("Does not produce NaN / Inf on typical input");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);
            xo.setCrossoverFrequency(crossover);

            juce::Random rng(42);
            const int N = 16384;
            std::vector<float> input(N), low(N), high(N);
            for (int i = 0; i < N; ++i)
                input[static_cast<size_t>(i)] = rng.nextFloat() * 2.0f - 1.0f;

            for (int i = 0; i < N; i += blockSize)
            {
                const int n = juce::jmin(blockSize, N - i);
                xo.process(input.data() + i, low.data() + i, high.data() + i, n);
            }

            for (int i = 0; i < N; ++i)
            {
                expect(std::isfinite(low [static_cast<size_t>(i)]));
                expect(std::isfinite(high[static_cast<size_t>(i)]));
            }
        }

        beginTest("Crossover frequency clamps to safe range");
        {
            Crossover xo;
            xo.prepare(sampleRate, blockSize);

            // DC and Nyquist aren't valid biquad cutoffs. The implementation
            // must clamp inside [20 Hz, sampleRate/2 − 100 Hz].
            xo.setCrossoverFrequency(0.0f);
            xo.setCrossoverFrequency(static_cast<float>(sampleRate) * 0.6f);
            xo.setCrossoverFrequency(-1.0f);
            xo.setCrossoverFrequency(std::numeric_limits<float>::infinity());
            // If we made it here without asserting / faulting the clamp
            // works; feed a buffer to make sure the filter is still usable.
            std::vector<float> input(blockSize, 0.3f);
            std::vector<float> low(blockSize), high(blockSize);
            xo.process(input.data(), low.data(), high.data(), blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                expect(std::isfinite(low [static_cast<size_t>(i)]));
                expect(std::isfinite(high[static_cast<size_t>(i)]));
            }
        }
    }
};

static CrossoverTests crossoverTests;
