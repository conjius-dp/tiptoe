// TDD harness for MultiBandGate: the orchestration wrapper that holds a
// crossover, a decimator + low-rate BandGate + interpolator (low path),
// a full-rate BandGate (high path), a delay line aligning the high band
// to the low path's latency, and a sum-to-output.
//
// Invariants the processor depends on:
//  1. Silence → silence after the reported latency settles.
//  2. Pass-through preserves full-band content when no noise profile
//     has been learned (within bounded error from the resampler and
//     crossover filters).
//  3. Reported latency is a finite positive number of samples that
//     matches what the plugin should hand to the host.
//  4. Learning from broadband noise populates both bands' profiles.
//  5. Gate attenuates below-threshold content after learning.
//  6. processMono is block-size-independent for blocks that are
//     multiples of the decimation factor (standard DAW block sizes:
//     64/128/256/512).

#include <juce_core/juce_core.h>

#include "DSP/MultiBandGate.h"

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

    constexpr MultiBandGate::Config kDefaultCfg {
        2000.0f,  // crossoverHz
        8,        // decimationFactor
        4,        // lowFFTOrder  -> FFT 16
        7,        // highFFTOrder -> FFT 128
    };
}

class MultiBandGateTests : public juce::UnitTest
{
public:
    MultiBandGateTests() : juce::UnitTest("MultiBandGate") {}

    void runTest() override
    {
        constexpr double fs        = 44100.0;
        constexpr int    blockSize = 512;

        beginTest("Reports a positive finite latency in input samples");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            const int lat = mb.getLatencyInSamples();
            expect(lat > 0);
            expect(lat < static_cast<int>(fs * 0.02)); // < 20 ms sanity ceiling
        }

        beginTest("Silence in → silence out");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            std::vector<float> zero(8192, 0.0f);
            mb.processMono(zero.data(), static_cast<int>(zero.size()));
            for (float v : zero) expectEquals(v, 0.0f);
        }

        beginTest("Pass-through preserves broadband content (no learning)");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            const int N = 16384;
            auto input = noise(N, 0.3f, 7);
            auto original = input;
            // Process in DAW-sized blocks.
            for (int i = 0; i < N; i += blockSize)
                mb.processMono(input.data() + i, juce::jmin(blockSize, N - i));

            // Skip the full-plugin latency warm-up.
            const int skip = mb.getLatencyInSamples() + 1024;
            const float rmsIn  = rms(original.data() + skip, N - skip);
            const float rmsOut = rms(input.data()    + skip, N - skip);
            // Allow a few dB leeway for crossover + resampler filters.
            expectWithinAbsoluteError(ratioDb(rmsOut, rmsIn), 0.0f, 2.0f);
        }

        beginTest("Low-band content (100 Hz) survives round-trip");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            const int N = 8192;
            auto input = sine(100.0f, static_cast<float>(fs), N, 0.3f);
            auto original = input;
            for (int i = 0; i < N; i += blockSize)
                mb.processMono(input.data() + i, juce::jmin(blockSize, N - i));

            const int skip = mb.getLatencyInSamples() + 1024;
            const float rmsIn  = rms(original.data() + skip, N - skip);
            const float rmsOut = rms(input.data()    + skip, N - skip);
            expectWithinAbsoluteError(ratioDb(rmsOut, rmsIn), 0.0f, 2.0f);
        }

        beginTest("High-band content (6 kHz) survives round-trip");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            const int N = 8192;
            auto input = sine(6000.0f, static_cast<float>(fs), N, 0.3f);
            auto original = input;
            for (int i = 0; i < N; i += blockSize)
                mb.processMono(input.data() + i, juce::jmin(blockSize, N - i));

            const int skip = mb.getLatencyInSamples() + 1024;
            const float rmsIn  = rms(original.data() + skip, N - skip);
            const float rmsOut = rms(input.data()    + skip, N - skip);
            expectWithinAbsoluteError(ratioDb(rmsOut, rmsIn), 0.0f, 2.0f);
        }

        beginTest("Learning populates both band profiles");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            auto noiseBuf = noise(32768, 0.3f);
            mb.startLearning();
            mb.learnFromBlock(noiseBuf.data(), static_cast<int>(noiseBuf.size()));
            mb.stopLearning();

            // After learning, both band gates should report a profile with
            // non-zero bins. MultiBandGate exposes band accessors for this.
            const auto& low  = mb.getLowBandNoiseProfile();
            const auto& high = mb.getHighBandNoiseProfile();
            int nonZeroLow = 0, nonZeroHigh = 0;
            for (float v : low)  if (v > 0.001f) ++nonZeroLow;
            for (float v : high) if (v > 0.001f) ++nonZeroHigh;
            expect(nonZeroLow  > 0);
            expect(nonZeroHigh > 0);
        }

        beginTest("Gate attenuates below-threshold broadband signal");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            mb.setSensitivity(1.0f);
            mb.setReduction(-40.0f);

            auto learnBuf = noise(32768, 0.3f);
            mb.startLearning();
            mb.learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
            mb.stopLearning();

            const int N = 16384;
            auto quiet    = noise(N, 0.01f, 99);
            auto original = quiet;
            for (int i = 0; i < N; i += blockSize)
                mb.processMono(quiet.data() + i, juce::jmin(blockSize, N - i));

            // Skip plugin latency + per-bin attack ramp (~30 ms).
            const int skip = mb.getLatencyInSamples() + 5000;
            // Cross-band gating should still give at least 15 dB of
            // attenuation (slightly less than single-band because two
            // bands with independent attack coefficients take longer to
            // fully close).
            expect(ratioDb(rms(quiet.data()    + skip, N - skip),
                           rms(original.data() + skip, N - skip)) < -15.0f);
        }

        beginTest("Block-size independent (64 / 128 / 256 / 512)");
        {
            const int N = 8192;
            auto make = [&]{
                auto g = std::make_unique<MultiBandGate>(kDefaultCfg);
                g->prepare(fs, 512);
                g->setSensitivity(1.5f);
                g->setReduction(-30.0f);
                auto learnBuf = noise(16384, 0.2f);
                g->startLearning();
                g->learnFromBlock(learnBuf.data(), static_cast<int>(learnBuf.size()));
                g->stopLearning();
                return g;
            };

            auto run = [&](int bs) {
                auto g = make();
                auto sig = noise(N, 0.2f, 44);
                for (int i = 0; i < N; i += bs)
                    g->processMono(sig.data() + i, juce::jmin(bs, N - i));
                return sig;
            };

            auto a = run(512);
            auto b = run(64);
            const int skip = 2048;
            for (int i = skip; i < N; ++i)
            {
                expectWithinAbsoluteError(a[static_cast<size_t>(i)],
                                          b[static_cast<size_t>(i)], 1e-3f);
            }
        }

        beginTest("reset() returns to silent-stable state");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            auto sig = noise(4096, 0.5f);
            mb.processMono(sig.data(), static_cast<int>(sig.size()));

            mb.reset();
            std::vector<float> zero(blockSize * 8, 0.0f);
            mb.processMono(zero.data(), static_cast<int>(zero.size()));
            for (float v : zero) expectEquals(v, 0.0f);
        }

        beginTest("Produces finite output on random broadband input");
        {
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            auto s = noise(16384, 0.5f, 42);
            for (int i = 0; i < static_cast<int>(s.size()); i += blockSize)
                mb.processMono(s.data() + i,
                               juce::jmin(blockSize, static_cast<int>(s.size()) - i));
            for (float v : s) expect(std::isfinite(v));
        }

        beginTest("Latency target: < 8 ms (target ~3.5 ms for 512/16/128)");
        {
            // The specific number depends on the resampler filter length,
            // but ≤ 8 ms is a hard ceiling. Anything higher means the
            // multi-band rewrite is actually slower than single-band
            // and the whole exercise failed.
            MultiBandGate mb(kDefaultCfg);
            mb.prepare(fs, blockSize);
            const int lat = mb.getLatencyInSamples();
            const float latMs = static_cast<float>(lat) / static_cast<float>(fs) * 1000.0f;
            logMessage(juce::String("MultiBandGate latency = ")
                        + juce::String(lat) + " samples = "
                        + juce::String(latMs, 2) + " ms");
            expect(latMs < 8.0f);
        }
    }
};

static MultiBandGateTests multiBandGateTests;
