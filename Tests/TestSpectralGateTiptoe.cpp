#include <juce_core/juce_core.h>
#include <cmath>
#include <random>
#include <chrono>
#include <iostream>
#include <cstdio>
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
            denoiser.setSensitivity(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.3f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
            auto original = quiet;

            denoiser.processMono(quiet.data(), numSamples);

            // Skip past both the FFT priming AND the per-bin gate's attack
            // ramp (~30 ms default × 3 time constants ≈ 4000 samples at
            // 44.1 kHz). Without this, the measurement window straddles the
            // ramp-in and the averaged RMS never reaches the steady-state
            // reduction floor.
            const int skip = 5000;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expect(db < -20.0f);
        }

        beginTest("Gating preserves signal above noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setSensitivity(1.0f);
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

        beginTest("Sensitivity parameter controls gating aggressiveness");
        {
            auto runWithThreshold = [](float threshold) -> float {
                SpectralGateTiptoe denoiser;
                denoiser.prepare(44100.0, 512);
                denoiser.setSensitivity(threshold);
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
                denoiser.setSensitivity(1.0f);
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
            denoiser.setSensitivity(1.0f);
            denoiser.setReduction(-40.0f);

            auto noise = generateWhiteNoise(32768, 0.3f);
            denoiser.startLearning();
            denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            denoiser.stopLearning();

            const int numSamples = 16384;
            auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
            auto original = quiet;

            denoiser.processMono(quiet.data(), numSamples);

            // Skip past the gate's per-bin attack ramp — see the other
            // "attenuates below noise floor" test for the rationale.
            const int skip = 5000;
            float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);
            float db = ratioToDb(outputRMS, inputRMS);
            expect(db < -20.0f);
        }

        beginTest("50% overlap: gating still preserves signal above noise floor");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setSensitivity(1.0f);
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

        beginTest("Initial effective sensitivity equals the set value (no initial glide)");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setSensitivity(1.0f);
            // Let the smoother converge
            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveSensitivityMultiplier(), 1.0f, 0.01f);
        }

        beginTest("setSensitivity does not instantly jump the effective value");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setSensitivity(1.0f);

            // Let the smoother converge to 1.0
            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveSensitivityMultiplier(), 1.0f, 0.01f);

            // Set a new target — the effective value must NOT have moved
            // before any audio is processed.
            gate.setSensitivity(5.0f);
            expectWithinAbsoluteError(gate.getEffectiveSensitivityMultiplier(), 1.0f, 0.01f);

            // After one FFT hop, the effective value must be partway between
            // the old value and the new target (not at either extreme).
            gate.processMono(zeros.data(), SpectralGateTiptoe::kHopSize);
            const float afterOneHop = gate.getEffectiveSensitivityMultiplier();
            expect(afterOneHop > 1.1f,
                   "effective threshold didn't move off the old value after one hop");
            expect(afterOneHop < 4.9f,
                   "effective threshold jumped straight to the new target (no smoothing)");
        }

        beginTest("Effective sensitivity converges to target after enough processing");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setSensitivity(1.0f);

            std::vector<float> zeros(4096, 0.0f);
            gate.processMono(zeros.data(), 4096);

            gate.setSensitivity(5.0f);
            // ~93 ms at 44.1 kHz — generous headroom over the 30 ms smoother.
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveSensitivityMultiplier(), 5.0f, 0.01f);
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

            // Partway through the 30 ms ramp. Use ~10 ms (≈ 441 samples at
            // 44.1 kHz) so the smoother has advanced a meaningful fraction
            // regardless of the FFT hop size.
            gate.processMono(zeros.data(), 441);
            const float partway = gate.getEffectiveReductionGain();
            expect(partway > 0.1f, "reduction gain didn't move off the old value");
            expect(partway < 0.95f, "reduction gain jumped straight to target");

            // Plenty of time to converge.
            gate.processMono(zeros.data(), 4096);
            expectWithinAbsoluteError(gate.getEffectiveReductionGain(), 1.0f, 0.01f);
        }

        // Phase 9: Artifact-reduction features
        //
        // Soft-knee gating (no binary flipping at threshold), per-bin
        // temporal smoothing with learned attack/release, spectral smoothing
        // across neighbouring bins, and over-subtraction. Together these
        // eliminate the high-frequency "musical noise" / birdies you hear
        // at threshold settings that leave the gate teetering on the edge.

        beginTest("Soft-knee: magnitude well below threshold -> reduction gain");
        {
            const float gain = SpectralGateTiptoe::softKneeGain(0.01f, 1.0f, 0.05f);
            expectWithinAbsoluteError(gain, 0.05f, 0.001f);
        }

        beginTest("Soft-knee: magnitude well above threshold -> 1.0");
        {
            const float gain = SpectralGateTiptoe::softKneeGain(100.0f, 1.0f, 0.05f);
            expectWithinAbsoluteError(gain, 1.0f, 0.001f);
        }

        beginTest("Soft-knee: magnitude AT threshold -> halfway between reduction and 1.0");
        {
            const float gain = SpectralGateTiptoe::softKneeGain(1.0f, 1.0f, 0.05f);
            const float expected = 0.5f * (0.05f + 1.0f);
            expectWithinAbsoluteError(gain, expected, 0.02f);
        }

        beginTest("Soft-knee: monotonic and smooth (no jumps between two close inputs)");
        {
            // Binary gating would produce a discontinuity at magSq == threshold.
            // Soft-knee must not.
            const float a = SpectralGateTiptoe::softKneeGain(0.99f, 1.0f, 0.05f);
            const float b = SpectralGateTiptoe::softKneeGain(1.01f, 1.0f, 0.05f);
            expect(b > a, "gain must increase with magnitude");
            expect(std::abs(b - a) < 0.1f,
                   "gain must not jump at the threshold boundary");
        }

        beginTest("Bin gain state starts at 1.0 (gate open)");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            for (int b = 0; b < SpectralGateTiptoe::kNumBins; ++b)
                expectWithinAbsoluteError(gate.getBinGainState(b), 1.0f, 1e-6f);
        }

        beginTest("Bin gain state smooths toward reduction over multiple frames");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setSensitivity(1.0f);
            gate.setReduction(-60.0f);

            // Learn a moderately loud noise profile so the quiet sine below
            // definitely falls under threshold.
            auto loud = generateWhiteNoise(32768, 0.5f);
            gate.startLearning();
            gate.learnFromBlock(loud.data(), static_cast<int>(loud.size()));
            gate.stopLearning();

            // Process quiet audio (below threshold) for ONE FFT hop only.
            std::vector<float> quiet(SpectralGateTiptoe::kHopSize, 0.0f);
            gate.processMono(quiet.data(), SpectralGateTiptoe::kHopSize);

            // After a single hop, at least one bin must have moved off 1.0
            // but not be all the way at reduction (smoothed, not instant).
            int partialCount = 0;
            for (int b = 1; b < SpectralGateTiptoe::kNumBins - 1; ++b)
            {
                const float g = gate.getBinGainState(b);
                if (g < 0.999f && g > 0.01f) ++partialCount;
            }
            expect(partialCount > 0,
                   "no bins were partially gated — smoothing did not apply");
        }

        beginTest("Volatility is 0 before learning, non-zero after learning noise");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);

            // Before learning.
            for (int b = 0; b < SpectralGateTiptoe::kNumBins; ++b)
                expectWithinAbsoluteError(gate.getBinVolatility(b), 0.0f, 1e-6f);

            auto noise = generateWhiteNoise(32768, 0.5f);
            gate.startLearning();
            gate.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            gate.stopLearning();

            // After learning white noise, at least one mid-spectrum bin has
            // measurable volatility.
            int nonZero = 0;
            for (int b = 4; b < SpectralGateTiptoe::kNumBins - 4; ++b)
                if (gate.getBinVolatility(b) > 0.01f) ++nonZero;
            expect(nonZero > 0,
                   "volatility was zero across the spectrum after learning noise");
        }

        beginTest("Attack/release ms default before learning, per-bin after");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);

            // Defaults before learning.
            const float defaultAttack = gate.getBinAttackMs(100);
            const float defaultRelease = gate.getBinReleaseMs(100);
            expect(defaultAttack > 0.0f && defaultRelease > 0.0f,
                   "default time constants must be positive");
            expect(defaultRelease > defaultAttack,
                   "release should be slower than attack by default");

            auto noise = generateWhiteNoise(32768, 0.3f);
            gate.startLearning();
            gate.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            gate.stopLearning();

            // Time constants may vary per bin after learning.
            const float learnedAttack = gate.getBinAttackMs(100);
            expect(learnedAttack > 0.0f, "learned attack must still be positive");
        }

        beginTest("Over-subtraction: below-threshold signal attenuated more than the reduction knob alone");
        {
            SpectralGateTiptoe gate;
            gate.prepare(44100.0, 512);
            gate.setSensitivity(1.0f);
            gate.setReduction(-20.0f); // 0.1x gain: with over-subtraction
                                       // the effective attenuation is deeper

            auto noise = generateWhiteNoise(32768, 0.3f);
            gate.startLearning();
            gate.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
            gate.stopLearning();

            const int numSamples = 16384;
            auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
            auto original = quiet;
            gate.processMono(quiet.data(), numSamples);

            const int skip = SpectralGateTiptoe::kFFTSize;
            const float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
            const float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);
            const float db = ratioToDb(outputRMS, inputRMS);
            // Without over-subtraction we'd expect ~ -20 dB. With the 1.5×
            // factor (and temporal smoothing) we should see meaningfully
            // deeper attenuation — but the signal should still be audible.
            expect(db < -15.0f, "gate did not attenuate enough");
        }

        // Performance
        beginTest("Must process 1s of audio in under 50ms");
        {
            SpectralGateTiptoe denoiser;
            denoiser.prepare(44100.0, 512);
            denoiser.setSensitivity(1.5f);
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

// Custom runner that flushes after every test class so we actually see
// which test class a Windows crash happens in (MSVC buffers stdio when
// redirected and silently drops output on fail-fast / abort()).
class FlushingUnitTestRunner : public juce::UnitTestRunner
{
public:
    void logMessage(const juce::String& message) override
    {
        std::cerr << message.toRawUTF8() << '\n';
        std::cerr.flush();
    }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::cerr << "[TiptoeTests] starting" << std::endl;

    FlushingUnitTestRunner runner;
    runner.setPassesAreLogged(false);

    // Run each registered test class individually so crash output is
    // attributable. getAllTests() returns in registration order.
    auto& all = juce::UnitTest::getAllTests();
    for (auto* t : all)
    {
        std::cerr << "[TiptoeTests] running: "
                  << t->getName().toRawUTF8() << std::endl;
        juce::Array<juce::UnitTest*> one;
        one.add(t);
        runner.runTests(one);
    }

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* result = runner.getResult(i);
        if (result != nullptr)
            failures += result->failures;
    }

    std::cerr << "[TiptoeTests] done — failures=" << failures << std::endl;
    return failures > 0 ? 1 : 0;
}
