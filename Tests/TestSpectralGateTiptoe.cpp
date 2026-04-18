#include <juce_core/juce_core.h>
#include <cmath>
#include <random>
#include <chrono>
#include "DSP/SpectralGateTiptoe.h"

static constexpr double kPi = 3.14159265358979323846;

static std::vector<float> generateSine(float freq, float sampleRate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<size_t>(i)] = amplitude * std::sin(2.0f * static_cast<float>(kPi) * freq * static_cast<float>(i) / sampleRate);
    return buf;
}

static float computeRMS(const float* data, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(std::sqrt(sum / numSamples));
}

static std::vector<float> generateWhiteNoise(int numSamples, float amplitude = 1.0f, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    std::vector<float> buf(static_cast<size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        buf[static_cast<size_t>(i)] = dist(rng);
    return buf;
}

static float ratioToDb(float output, float input)
{
    return 20.0f * std::log10(output / (input + 1e-10f));
}

class SpectralGateTiptoeTests : public juce::UnitTest
{
public:
    SpectralGateTiptoeTests() : juce::UnitTest("Tiptoe") {}

    void runTest() override
    {
        // Phase 1: FFT Round-Trip
        beginTest("Silence in produces silence out");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            std::vector<float> silence(4096, 0.0f);
            denoiser.processMono(silence.data(), static_cast<int>(silence.size()));

            for (int i = 0; i < static_cast<int>(silence.size()); ++i)
                expectWithinAbsoluteError(silence[static_cast<size_t>(i)], 0.0f, 1e-6f);
        }

        beginTest("Pass-through with no noise profile does not alter signal");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            const int numSamples = 16384;
            auto input = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
            auto original = input;

            denoiser.processMono(input.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(input.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expectWithinAbsoluteError(db, 0.0f, 1.0f);
        }

        // Phase 2: Noise Learning
        beginTest("Learning state flags work correctly");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            expect(!denoiser.isLearning());
            denoiser.startLearning();
            expect(denoiser.isLearning());
            denoiser.stopLearning();
            expect(!denoiser.isLearning());
        }

        beginTest("Learning from silence produces near-zero profile");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            std::vector<float> silence(8192, 0.0f);
            denoiser.startLearning();
            denoiser.learnFromBlock(silence.data(), static_cast<int>(silence.size()));
            denoiser.stopLearning();

            auto& profile = denoiser.getNoiseProfile();
            expectEquals(static_cast<int>(profile.size()), SpectralGateTiptoe::kNumBins);

            for (size_t i = 0; i < profile.size(); ++i)
                expectWithinAbsoluteError(profile[i], 0.0f, 1e-6f);
        }

        beginTest("Learning accumulates noise profile from white noise");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            auto noise = generateWhiteNoise(32768, 0.5f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            auto& profile = denoiser.getNoiseProfile();
            expectEquals(static_cast<int>(profile.size()), SpectralGateTiptoe::kNumBins);

            for (size_t i = 1; i < profile.size() - 1; ++i)
                expect(profile[i] > 0.0f);
        }

        beginTest("Multiple learning sessions reset accumulator");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            auto loudNoise = generateWhiteNoise(8192, 1.0f, 1);
            denoiser.startLearning();
            denoiser.learnFromBlock(loudNoise.data(), static_cast<int>(loudNoise.size()));
            denoiser.stopLearning();

            std::vector<float> silence(8192, 0.0f);
            denoiser.startLearning();
            denoiser.learnFromBlock(silence.data(), static_cast<int>(silence.size()));
            denoiser.stopLearning();

            auto& profile = denoiser.getNoiseProfile();
            for (size_t i = 0; i < profile.size(); ++i)
                expectWithinAbsoluteError(profile[i], 0.0f, 1e-6f);
        }

        // Phase 3: Spectral Gating
        beginTest("Gating attenuates signal below noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setThreshold(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.3f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
            auto original = quiet;

            denoiser.processMono(quiet.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expect(db < -20.0f);
        }

        beginTest("Gating preserves signal above noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setThreshold(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.01f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto loud = generateSine(1000.0f, 44100.0f, numSamples, 0.8f);
            auto original = loud;

            denoiser.processMono(loud.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(loud.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expectWithinAbsoluteError(db, 0.0f, 2.0f);
        }

        beginTest("Threshold parameter controls gating aggressiveness");
        {
            auto runWithThreshold = [](float threshold) -> float {
                SpectralGateTiptoe denoiser;
                denoiser.prepare(44100.0, 512);
                denoiser.setThreshold(threshold);
                denoiser.setReduction(-40.0f);

                auto noise = generateWhiteNoise(32768, 0.1f);
                denoiser.startLearning();
                denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
                denoiser.stopLearning();

                const int numSamples = 16384;
                auto signal = generateWhiteNoise(numSamples, 0.15f, 200);
                denoiser.processMono(signal.data(), numSamples);

                const int skip = SpectralGateTiptoe::kFFTSize;
                return computeRMS(signal.data() + skip, numSamples - skip);
            };

            float rmsLow = runWithThreshold(1.0f);
            float rmsHigh = runWithThreshold(5.0f);
            expect(rmsHigh < rmsLow);
        }

        beginTest("Reduction parameter controls attenuation depth");
        {
            auto runWithReduction = [](float reductionDb) -> float {
                SpectralGateTiptoe denoiser;
                denoiser.prepare(44100.0, 512);
                denoiser.setThreshold(1.0f);
                denoiser.setReduction(reductionDb);

                auto noise = generateWhiteNoise(32768, 0.3f);
                denoiser.startLearning();
                denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
                denoiser.stopLearning();

                const int numSamples = 16384;
                auto signal = generateWhiteNoise(numSamples, 0.01f, 77);
                denoiser.processMono(signal.data(), numSamples);

                const int skip = SpectralGateTiptoe::kFFTSize;
                return computeRMS(signal.data() + skip, numSamples - skip);
            };

            float rmsShallow = runWithReduction(-6.0f);
            float rmsDeep = runWithReduction(-40.0f);
            expect(rmsDeep < rmsShallow);
        }

        // Phase 4: Overlap-Add Continuity
        beginTest("Processing multiple small blocks matches one large block");
        {
            const int totalSamples = 8192;
            auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

            SpectralGateTiptoe d1;
            d1.prepare(44100.0, totalSamples);
            auto large = signal;
            d1.processMono(large.data(), totalSamples);

            SpectralGateTiptoe d2;
            d2.prepare(44100.0, 128);
            auto small = signal;
            for (int i = 0; i < totalSamples; i += 128)
                d2.processMono(small.data() + i, 128);

            const int skip = SpectralGateTiptoe::kFFTSize;
            for (int i = skip; i < totalSamples; ++i)
                expectWithinAbsoluteError(small[static_cast<size_t>(i)], large[static_cast<size_t>(i)], 1e-5f);
        }

        beginTest("No clicks or discontinuities at block boundaries");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 256);

            const int blockSize = 256;
            const int numBlocks = 32;
            const int totalSamples = blockSize * numBlocks;
            auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

            for (int i = 0; i < totalSamples; i += blockSize)
                denoiser.processMono(signal.data() + i, blockSize);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float maxJump = 0.0f;
            for (int i = skip + 1; i < totalSamples; ++i)
            {
                float jump = std::abs(signal[static_cast<size_t>(i)] - signal[static_cast<size_t>(i - 1)]);
                maxJump = std::max(maxJump, jump);
            }
            expect(maxJump < 0.1f);
        }

        // Phase 5: Reset
        beginTest("prepare and reset return to clean state");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            auto noise = generateWhiteNoise(8192, 0.5f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            auto signal = generateSine(440.0f, 44100.0f, 4096, 0.5f);
            denoiser.processMono(signal.data(), 4096);

            denoiser.reset();

            auto& profile = denoiser.getNoiseProfile();
            for (size_t i = 0; i < profile.size(); ++i)
                expectWithinAbsoluteError(profile[i], 0.0f, 1e-6f);

            const int numSamples = 16384;
            auto fresh = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
            auto original = fresh;
            denoiser.processMono(fresh.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(fresh.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expectWithinAbsoluteError(db, 0.0f, 1.0f);
        }

        // Phase 6: Performance (50% overlap)
        beginTest("50% overlap: silence in produces silence out");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            std::vector<float> silence(4096, 0.0f);
            denoiser.processMono(silence.data(), static_cast<int>(silence.size()));

            for (int i = 0; i < static_cast<int>(silence.size()); ++i)
                expectWithinAbsoluteError(silence[static_cast<size_t>(i)], 0.0f, 1e-6f);
        }

        beginTest("50% overlap: pass-through preserves signal");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            const int numSamples = 16384;
            auto input = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
            auto original = input;

            denoiser.processMono(input.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(input.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expectWithinAbsoluteError(db, 0.0f, 1.0f);
        }

        beginTest("50% overlap: gating still attenuates below noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setThreshold(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.3f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
            auto original = quiet;

            denoiser.processMono(quiet.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expect(db < -20.0f);
        }

        beginTest("50% overlap: gating still preserves signal above noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setThreshold(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.01f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto loud = generateSine(1000.0f, 44100.0f, numSamples, 0.8f);
            auto original = loud;

            denoiser.processMono(loud.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(loud.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expectWithinAbsoluteError(db, 0.0f, 2.0f);
        }

        beginTest("50% overlap: small blocks match large block");
        {
            const int totalSamples = 8192;
            auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

            SpectralGateTiptoe d1;
            d1.prepare(44100.0, totalSamples);
            auto large = signal;
            d1.processMono(large.data(), totalSamples);

            SpectralGateTiptoe d2;
            d2.prepare(44100.0, 128);
            auto small = signal;
            for (int i = 0; i < totalSamples; i += 128)
                d2.processMono(small.data() + i, 128);

            const int skip = SpectralGateTiptoe::kFFTSize;
            for (int i = skip; i < totalSamples; ++i)
                expectWithinAbsoluteError(small[static_cast<size_t>(i)], large[static_cast<size_t>(i)], 1e-5f);
        }

        // Phase 7: Processing Time Measurement
        beginTest("getLastProcessingTimeMs returns zero before any processing");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            expectWithinAbsoluteError(denoiser.getLastProcessingTimeMs(), 0.0f, 1e-6f);
        }

        beginTest("getLastProcessingTimeMs returns positive value after processing");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            auto signal = generateWhiteNoise(4096, 0.5f);
            denoiser.processMono(signal.data(), static_cast<int>(signal.size()));
            expect(denoiser.getLastProcessingTimeMs() > 0.0f);
        }

        beginTest("getLastProcessingTimeMs updates each call");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);

            auto small = generateWhiteNoise(64, 0.5f);
            denoiser.processMono(small.data(), static_cast<int>(small.size()));
            float timeSmall = denoiser.getLastProcessingTimeMs();
            expect(timeSmall > 0.0f);

            auto large = generateWhiteNoise(44100, 0.5f);
            denoiser.processMono(large.data(), static_cast<int>(large.size()));
            float timeLarge = denoiser.getLastProcessingTimeMs();
            expect(timeLarge > 0.0f);
            expect(timeLarge > timeSmall);
        }

        // Phase 8: Parameter smoothing
        //
        // DAW automation and knob drags feed the processor a sequence of
        // discrete values. Without smoothing, each processBlock jumps the
        // gate's threshold / reduction instantly, which produces frame-level
        // granularity at the FFT hop rate. The smoother caps the rate at
        // which those parameters change so automation is audibly smooth.

        beginTest("Initial effective threshold equals the set value (no initial glide)");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setThreshold(1.0f);
            // Let the smoother converge
            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveThresholdMultiplier(), 1.0f, 0.01f);
        }

        beginTest("setThreshold does not instantly jump the effective value");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setThreshold(1.0f);

            // Let the smoother converge to 1.0
            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveThresholdMultiplier(), 1.0f, 0.01f);

            // Set a new target — the effective value must NOT have moved
            // before any audio is processed.
            gate.setThreshold(5.0f);
            expectWithinAbsoluteError(gate.getEffectiveThresholdMultiplier(), 1.0f, 0.01f);

            // After one FFT hop, the effective value must be partway between
            // the old value and the new target (not at either extreme).
            gate.processMono(zeros.data(), SpectralGateTiptoe::kHopSize);
            const float afterOneHop = gate.getEffectiveThresholdMultiplier();
            expect(afterOneHop > 1.1f,
                   "effective threshold didn't move off the old value after one hop");
            expect(afterOneHop < 4.9f,
                   "effective threshold jumped straight to the new target (no smoothing)");
        }

        beginTest("Effective threshold converges to target after enough processing");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setThreshold(1.0f);

            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);

            gate.setThreshold(5.0f);
            // ~93 ms at 44.1 kHz — generous headroom over the 30 ms smoother.
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveThresholdMultiplier(), 5.0f, 0.01f);
        }

        beginTest("setReduction does not instantly jump the effective gain");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setReduction(-60.0f); // gain = 0.001

            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);
            const float initialGain = std::pow(10.0f, -60.0f / 20.0f);
            expectWithinAbsoluteError(gate.getEffectiveReductionGain(), initialGain, 0.002f);

            // Jump to 0 dB (full pass) — effective should not yet move.
            gate.setReduction(0.0f);
            expectWithinAbsoluteError(gate.getEffectiveReductionGain(), initialGain, 0.002f);

            // After one hop, partway.
            gate.processMono(zeros.data(), SpectralGateTiptoe::kHopSize);
            const float afterOneHop = gate.getEffectiveReductionGain();
            expect(afterOneHop > 0.1f, "reduction gain didn't move off the old value");
            expect(afterOneHop < 0.95f, "reduction gain jumped straight to target");

            // Plenty of time to converge.
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveReductionGain(), 1.0f, 0.01f);
        }

        // Performance
        beginTest("Must process 1s of audio in under 50ms");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setThreshold(1.5f);
            denoiser.setReduction(-30.0f);

            auto noise = generateWhiteNoise(32768, 0.3f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            auto signal = generateWhiteNoise(44100, 0.5f, 123);

            // Warm up
            denoiser.processMono(signal.data(), 44100);

            auto start = std::chrono::high_resolution_clock::now();
            denoiser.processMono(signal.data(), 44100);
            auto end = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            expect(ms < 50.0);
        }
    }
};

static SpectralGateTiptoeTests spectralGateTiptoeTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* result = runner.getResult(i);
        if (result != nullptr)
            failures += result->failures;
    }

    return failures > 0 ? 1 : 0;
}
