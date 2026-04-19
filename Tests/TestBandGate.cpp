// TDD harness for BandGate: a spectral gate with runtime-configurable
// FFT order. Each band of the multi-band gate owns one instance at its
// own FFT size (low band: 16 at 5.5 kHz, high band: 128 at 44.1 kHz).
//
// The invariants here are a trimmed mirror of the original
// SpectralGateTiptoe tests, re-verified for multiple FFT orders because
// the whole point of this class is that it works at any power-of-two
// frame size from 16 up through 2048.

#include <juce_core/juce_core.h>

#include "DSP/BandGate.h"

namespace
{
    inline std::vector<float> sine(float freqHz, float fs, int n, float amp = 0.5f)
    {
        std::vector<float> out(static_cast<size_t>(n));
        const float step = juce::MathConstants<float>::twoPi * freqHz / fs;
        float phase = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            out[static_cast<size_t>(i)] = amp * std::sin(phase);
            phase += step;
            if (phase > juce::MathConstants<float>::twoPi)
                phase -= juce::MathConstants<float>::twoPi;
        }
        return out;
    }

    inline std::vector<float> noise(int n, float amp, int seed = 1)
    {
        std::vector<float> out(static_cast<size_t>(n));
        juce::Random rng(seed);
        for (int i = 0; i < n; ++i)
            out[static_cast<size_t>(i)] = (rng.nextFloat() * 2.0f - 1.0f) * amp;
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

class BandGateTests : public juce::UnitTest
{
public:
    BandGateTests() : juce::UnitTest("BandGate") {}

    void runTest() override
    {
        // --- Construction & sizing --------------------------------------
        beginTest("Constructor sets FFT size from order");
        {
            BandGate g4 (4); expectEquals(g4.getFFTSize(),  16);
            BandGate g7 (7); expectEquals(g7.getFFTSize(), 128);
            BandGate g9 (9); expectEquals(g9.getFFTSize(), 512);

            expectEquals(g4.getHopSize(), g4.getFFTSize() / 4);
            expectEquals(g7.getHopSize(), g7.getFFTSize() / 4);
            expectEquals(g9.getHopSize(), g9.getFFTSize() / 4);

            expectEquals(g4.getNumBins(), g4.getFFTSize() / 2 + 1);
            expectEquals(g7.getNumBins(), g7.getFFTSize() / 2 + 1);
        }

        beginTest("Constructor clamps degenerate orders");
        {
            // Floor at 4 (FFT 16), ceiling at 11 (FFT 2048). Anything
            // outside gets clamped — an FFT-of-2 gate is nonsense and
            // an FFT-of-65536 would break our prepare-time allocations.
            BandGate tooSmall(0);
            BandGate tooBig  (15);
            expect(tooSmall.getFFTSize() >= 16);
            expect(tooBig   .getFFTSize() <= 2048);
        }

        // --- Silence → silence -----------------------------------------
        beginTest("Silence in → silence out (no noise profile)");
        {
            for (int order : { 4, 7, 9 })
            {
                BandGate g(order);
                g.prepare(44100.0, 512);
                std::vector<float> zero(4096, 0.0f);
                g.processMono(zero.data(), 4096);
                for (float v : zero) expectEquals(v, 0.0f);
            }
        }

        // --- Pass-through when no noise profile has been learned -------
        beginTest("No noise profile → signal passes through (amplitude ≈ input)");
        {
            for (int order : { 4, 7, 9 })
            {
                BandGate g(order);
                g.prepare(44100.0, 512);
                const int N = 8192;
                // Pick a frequency well inside each gate's representable
                // range (above DC, below Nyquist): 1 kHz works for all.
                auto s = sine(1000.0f, 44100.0f, N, 0.5f);
                auto original = s;
                g.processMono(s.data(), N);

                // Skip FFT priming.
                const int skip = g.getFFTSize() * 2;
                const float rmsIn  = rms(original.data() + skip, N - skip);
                const float rmsOut = rms(s.data()        + skip, N - skip);
                expectWithinAbsoluteError(ratioDb(rmsOut, rmsIn), 0.0f, 1.0f);
            }
        }

        // --- Noise profile learning ------------------------------------
        beginTest("Learning populates the noise profile");
        {
            BandGate g(7);
            g.prepare(44100.0, 512);
            auto n = noise(16384, 0.3f);
            g.startLearning();
            g.learnFromBlock(n.data(), static_cast<int>(n.size()));
            g.stopLearning();
            const auto& profile = g.getNoiseProfile();
            expectEquals(static_cast<int>(profile.size()), g.getNumBins());
            // Broadband noise should give non-trivial energy in most bins.
            int nonEmpty = 0;
            for (float v : profile) if (v > 0.001f) ++nonEmpty;
            expect(nonEmpty > g.getNumBins() / 2);
        }

        beginTest("Learning from silence produces a ~zero profile");
        {
            BandGate g(7);
            g.prepare(44100.0, 512);
            std::vector<float> zero(8192, 0.0f);
            g.startLearning();
            g.learnFromBlock(zero.data(), static_cast<int>(zero.size()));
            g.stopLearning();
            for (float v : g.getNoiseProfile())
                expectWithinAbsoluteError(v, 0.0f, 1e-5f);
        }

        // --- Gate attenuates below-threshold, passes above -------------
        beginTest("Gate attenuates below-threshold signal");
        {
            // Learn on 0.3-amplitude noise, then run a 0.01-amplitude
            // noise through. Output must be at least 20 dB quieter.
            BandGate g(9);
            g.prepare(44100.0, 512);
            g.setSensitivity(1.0f);
            g.setReduction(-40.0f);

            auto learnBuf = noise(32768, 0.3f);
            g.startLearning();
            g.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
            g.stopLearning();

            const int N = 16384;
            auto quiet    = noise(N, 0.01f, 99);
            auto original = quiet;
            g.processMono(quiet.data(), N);

            // Skip the per-bin attack ramp (~30 ms = 1323 samples at 44.1 kHz).
            const int skip = 5000;
            expect(ratioDb(rms(quiet.data()    + skip, N - skip),
                           rms(original.data() + skip, N - skip)) < -20.0f);
        }

        beginTest("Gate preserves above-threshold signal");
        {
            BandGate g(9);
            g.prepare(44100.0, 512);
            g.setSensitivity(1.0f);
            g.setReduction(-40.0f);

            auto learnBuf = noise(32768, 0.01f);
            g.startLearning();
            g.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
            g.stopLearning();

            const int N = 16384;
            auto loud     = sine(1000.0f, 44100.0f, N, 0.8f);
            auto original = loud;
            g.processMono(loud.data(), N);

            const int skip = g.getFFTSize();
            expectWithinAbsoluteError(
                ratioDb(rms(loud.data()     + skip, N - skip),
                        rms(original.data() + skip, N - skip)),
                0.0f, 2.0f);
        }

        // --- Sensitivity knob behaviour --------------------------------
        beginTest("Higher sensitivity gates more aggressively");
        {
            auto runWith = [](float sensitivity) {
                BandGate g(9);
                g.prepare(44100.0, 512);
                g.setSensitivity(sensitivity);
                g.setReduction(-40.0f);
                auto learnBuf = noise(32768, 0.1f);
                g.startLearning();
                g.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
                g.stopLearning();

                const int N = 16384;
                auto s = noise(N, 0.15f, 200);
                g.processMono(s.data(), N);
                return rms(s.data() + g.getFFTSize(), N - g.getFFTSize());
            };
            expect(runWith(5.0f) < runWith(1.0f));
        }

        // --- Block-size independence -----------------------------------
        beginTest("Small-block and large-block processing match");
        {
            BandGate ga(7), gb(7);
            ga.prepare(44100.0, 1024);
            gb.prepare(44100.0, 1024);
            ga.setSensitivity(1.5f); ga.setReduction(-30.0f);
            gb.setSensitivity(1.5f); gb.setReduction(-30.0f);

            auto learnBuf = noise(32768, 0.1f);
            ga.startLearning(); gb.startLearning();
            ga.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
            gb.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
            ga.stopLearning(); gb.stopLearning();

            const int N = 8192;
            auto bigBlock   = noise(N, 0.2f, 7);
            auto smallBlock = bigBlock;
            ga.processMono(bigBlock.data(), N);
            for (int i = 0; i < N; i += 32)
                gb.processMono(smallBlock.data() + i, juce::jmin(32, N - i));

            // Results must match within rounding — the per-block boundary
            // shouldn't matter to OA reconstruction.
            const int skip = ga.getFFTSize();
            for (int i = skip; i < N; ++i)
                expectWithinAbsoluteError(
                    bigBlock [static_cast<size_t>(i)],
                    smallBlock[static_cast<size_t>(i)], 1e-4f);
        }

        // --- Reset ------------------------------------------------------
        beginTest("reset() clears processing state to silent-stable");
        {
            BandGate g(7);
            g.prepare(44100.0, 512);
            auto buf = noise(4096, 0.5f);
            g.processMono(buf.data(), static_cast<int>(buf.size()));

            g.reset();
            std::vector<float> zero(1024, 0.0f);
            g.processMono(zero.data(), 1024);
            for (float v : zero) expectEquals(v, 0.0f);
        }

        // --- Edge cases -------------------------------------------------
        beginTest("Produces finite output on random broadband input");
        {
            BandGate g(9);
            g.prepare(44100.0, 512);
            auto s = noise(16384, 0.5f, 42);
            g.processMono(s.data(), static_cast<int>(s.size()));
            for (float v : s) expect(std::isfinite(v));
        }

        beginTest("Works at multiple sample rates (22.05 / 44.1 / 48 / 96 kHz)");
        {
            for (double fs : { 22050.0, 44100.0, 48000.0, 96000.0 })
            {
                BandGate g(7);
                g.prepare(fs, 512);
                auto s = sine(1000.0f, static_cast<float>(fs), 8192, 0.3f);
                g.processMono(s.data(), 8192);
                for (float v : s) expect(std::isfinite(v));
            }
        }
    }
};

static BandGateTests bandGateTests;
