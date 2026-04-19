// TDD harness for the decimator / interpolator pair used to run the
// low-band gate at 1/D of the input sample rate.
//
// Invariants the multi-band path needs:
//  1. Decimator output is fs/D with input anti-alias filtered.
//  2. Interpolator output is fs with image rejection applied.
//  3. Decimate-then-interpolate a full-band signal reconstructs the
//     in-band part (below the anti-alias cutoff) with ≤ 1 dB error
//     and kills out-of-band energy.
//  4. DC through the pair is preserved.
//  5. Silence in → silence out.
//  6. Combined group delay is stable and deterministic (needed to
//     align the low and high bands in the multi-band gate).
//  7. No heap churn on the process path (no std::vector resize, no
//     allocation — verified by fixed buffer sizes in the tests).

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "DSP/Decimator.h"
#include "DSP/Interpolator.h"

namespace
{
    inline std::vector<float> sine(float freqHz, float fs, int n)
    {
        std::vector<float> out(static_cast<size_t>(n));
        const float step = juce::MathConstants<float>::twoPi * freqHz / fs;
        float phase = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            out[static_cast<size_t>(i)] = std::sin(phase);
            phase += step;
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
        }
        return out;
    }

    inline float rms(const float* x, int n)
    {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += static_cast<double>(x[i]) * x[i];
        return static_cast<float>(std::sqrt(s / std::max(1, n)));
    }

    inline float ratioDb(float num, float den)
    {
        return 20.0f * std::log10(std::max(num, 1e-12f) / std::max(den, 1e-12f));
    }
}

class ResamplerTests : public juce::UnitTest
{
public:
    ResamplerTests() : juce::UnitTest("Resampler") {}

    void runTest() override
    {
        constexpr double fsIn     = 44100.0;
        constexpr int    D        = 8;
        constexpr int    blockIn  = 512;
        constexpr int    blockOut = blockIn / D;

        beginTest("Decimator output size is input / D");
        {
            Decimator dec;
            dec.prepare(fsIn, blockIn, D);
            std::vector<float> in(blockIn, 0.0f);
            std::vector<float> out(blockOut, 0.0f);
            const int written = dec.process(in.data(), out.data(), blockIn);
            expectEquals(written, blockOut);
        }

        beginTest("Interpolator output size is input * D");
        {
            Interpolator up;
            up.prepare(fsIn, blockOut, D);
            std::vector<float> in(blockOut, 0.0f);
            std::vector<float> out(blockIn, 0.0f);
            const int written = up.process(in.data(), out.data(), blockOut);
            expectEquals(written, blockIn);
        }

        beginTest("Silence in → silence out (decimator)");
        {
            Decimator dec;
            dec.prepare(fsIn, blockIn, D);
            std::vector<float> zero(blockIn, 0.0f);
            std::vector<float> out(blockOut, 0.0f);
            dec.process(zero.data(), out.data(), blockIn);
            for (int i = 0; i < blockOut; ++i)
                expectEquals(out[static_cast<size_t>(i)], 0.0f);
        }

        beginTest("Silence in → silence out (interpolator)");
        {
            Interpolator up;
            up.prepare(fsIn, blockOut, D);
            std::vector<float> zero(blockOut, 0.0f);
            std::vector<float> out(blockIn, 0.0f);
            up.process(zero.data(), out.data(), blockOut);
            for (int i = 0; i < blockIn; ++i)
                expectEquals(out[static_cast<size_t>(i)], 0.0f);
        }

        beginTest("In-band sine (100 Hz) survives round-trip ≤ 1 dB");
        {
            Decimator dec;   dec.prepare(fsIn, blockIn,  D);
            Interpolator up; up.prepare(fsIn, blockOut, D);

            const int N = 8192;
            auto input = sine(100.0f, static_cast<float>(fsIn), N);
            std::vector<float> decimated(N / D, 0.0f);
            std::vector<float> restored (N,     0.0f);

            // Stream in blocks; never alloc on the process path.
            std::vector<float> decBuf(blockOut), upBuf(blockIn);
            int decOffset = 0, resOffset = 0;
            for (int i = 0; i < N; i += blockIn)
            {
                const int n   = juce::jmin(blockIn, N - i);
                const int got = dec.process(input.data() + i, decBuf.data(), n);
                std::copy_n(decBuf.data(), got, decimated.data() + decOffset);
                decOffset += got;
                const int made = up.process(decBuf.data(), upBuf.data(), got);
                std::copy_n(upBuf.data(), made, restored.data() + resOffset);
                resOffset += made;
            }

            // Skip transient (decimator + interpolator group-delay warm-up).
            const int skip = 2048;
            const float rmsIn  = rms(input.data()    + skip, N - skip);
            const float rmsOut = rms(restored.data() + skip, N - skip);
            expectWithinAbsoluteError(ratioDb(rmsOut, rmsIn), 0.0f, 1.0f);
        }

        beginTest("Out-of-band sine (10 kHz) suppressed by ≥ 40 dB");
        {
            // 10 kHz input, fs/D/2 = 2756 Hz Nyquist → well above. The
            // anti-alias filter must kill this before it aliases.
            Decimator dec;   dec.prepare(fsIn, blockIn,  D);
            Interpolator up; up.prepare(fsIn, blockOut, D);

            const int N = 8192;
            auto input = sine(10000.0f, static_cast<float>(fsIn), N);
            std::vector<float> decBuf(blockOut), upBuf(blockIn);
            std::vector<float> restored(N, 0.0f);
            int resOffset = 0;
            for (int i = 0; i < N; i += blockIn)
            {
                const int n   = juce::jmin(blockIn, N - i);
                const int got = dec.process(input.data() + i, decBuf.data(), n);
                const int made = up.process(decBuf.data(), upBuf.data(), got);
                std::copy_n(upBuf.data(), made, restored.data() + resOffset);
                resOffset += made;
            }

            const int skip = 2048;
            const float rmsIn  = rms(input.data()    + skip, N - skip);
            const float rmsOut = rms(restored.data() + skip, N - skip);
            expect(ratioDb(rmsOut, rmsIn) < -40.0f);
        }

        beginTest("Round-trip preserves DC");
        {
            Decimator dec;   dec.prepare(fsIn, blockIn,  D);
            Interpolator up; up.prepare(fsIn, blockOut, D);

            const int N = 4096;
            std::vector<float> input(N, 0.7f);
            std::vector<float> decBuf(blockOut), upBuf(blockIn);
            std::vector<float> restored(N, 0.0f);
            int resOffset = 0;
            for (int i = 0; i < N; i += blockIn)
            {
                const int n   = juce::jmin(blockIn, N - i);
                const int got = dec.process(input.data() + i, decBuf.data(), n);
                const int made = up.process(decBuf.data(), upBuf.data(), got);
                std::copy_n(upBuf.data(), made, restored.data() + resOffset);
                resOffset += made;
            }

            // Mean should come back around 0.7 once settled.
            double sum = 0.0;
            const int skip = 2048;
            for (int i = skip; i < N; ++i) sum += restored[static_cast<size_t>(i)];
            expectWithinAbsoluteError(
                static_cast<float>(sum / (N - skip)), 0.7f, 0.05f);
        }

        beginTest("Combined group delay is reported and positive");
        {
            Decimator dec;   dec.prepare(fsIn, blockIn,  D);
            Interpolator up; up.prepare(fsIn, blockOut, D);
            // Group delay is the offset we need to use when aligning low
            // and high bands in the multi-band gate. Must be >0 and a
            // finite integer number of input-rate samples.
            const int delay = dec.getLatencyInputSamples()
                            + up .getLatencyInputSamples();
            expect(delay > 0);
            expect(delay < static_cast<int>(fsIn * 0.01)); // < 10 ms
        }

        beginTest("Produces finite output on random broadband input");
        {
            Decimator dec;   dec.prepare(fsIn, blockIn,  D);
            Interpolator up; up.prepare(fsIn, blockOut, D);
            juce::Random rng(2);
            const int N = 16384;
            std::vector<float> input(N);
            for (int i = 0; i < N; ++i)
                input[static_cast<size_t>(i)] = rng.nextFloat() * 2.0f - 1.0f;

            std::vector<float> decBuf(blockOut), upBuf(blockIn);
            for (int i = 0; i < N; i += blockIn)
            {
                const int n   = juce::jmin(blockIn, N - i);
                const int got = dec.process(input.data() + i, decBuf.data(), n);
                up.process(decBuf.data(), upBuf.data(), got);
                for (int j = 0; j < got;       ++j) expect(std::isfinite(decBuf[(size_t) j]));
                for (int j = 0; j < got * D;   ++j) expect(std::isfinite(upBuf [(size_t) j]));
            }
        }
    }
};

static ResamplerTests resamplerTests;
