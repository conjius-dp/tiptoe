#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>
#include <cmath>
#include <numeric>
#include <random>
#include <chrono>
#include "DSP/SpectralGateDenoiser.h"


static constexpr double kPi = 3.14159265358979323846;

// Helper: generate a sine wave
static std::vector<float> generateSine(float freq, float sampleRate, int numSamples, float amplitude = 1.0f)
{
    std::vector<float> buf(numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf[i] = amplitude * std::sin(2.0f * static_cast<float>(kPi) * freq * static_cast<float>(i) / sampleRate);
    return buf;
}

// Helper: compute RMS of a buffer
static float computeRMS(const float* data, int numSamples)
{
    double sum = 0.0;
    for (int i = 0; i < numSamples; ++i)
        sum += static_cast<double>(data[i]) * static_cast<double>(data[i]);
    return static_cast<float>(std::sqrt(sum / numSamples));
}

// Helper: generate white noise
static std::vector<float> generateWhiteNoise(int numSamples, float amplitude = 1.0f, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    std::vector<float> buf(numSamples);
    for (int i = 0; i < numSamples; ++i)
        buf[i] = dist(rng);
    return buf;
}

// ============================================================
// Phase 1: FFT Round-Trip
// ============================================================

TEST_CASE("Silence in produces silence out", "[phase1]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    std::vector<float> silence(4096, 0.0f);
    denoiser.processMono(silence.data(), static_cast<int>(silence.size()));

    for (int i = 0; i < static_cast<int>(silence.size()); ++i)
        REQUIRE(silence[i] == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Pass-through with no noise profile does not alter signal", "[phase1]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    // Generate a sine wave - need enough samples for overlap-add to stabilize
    const int numSamples = 16384;
    auto input = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
    auto original = input; // copy

    denoiser.processMono(input.data(), numSamples);

    // Skip the first FFT window of samples (latency from overlap-add)
    // Compare the steady-state portion
    const int skipSamples = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skipSamples, numSamples - skipSamples);
    float outputRMS = computeRMS(input.data() + skipSamples, numSamples - skipSamples);

    // Output RMS should be within 1 dB of input RMS
    float ratioDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(ratioDb == Catch::Approx(0.0f).margin(1.0f));
}

// ============================================================
// Phase 2: Noise Learning
// ============================================================

TEST_CASE("Learning state flags work correctly", "[phase2]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    REQUIRE_FALSE(denoiser.isLearning());
    denoiser.startLearning();
    REQUIRE(denoiser.isLearning());
    denoiser.stopLearning();
    REQUIRE_FALSE(denoiser.isLearning());
}

TEST_CASE("Learning from silence produces near-zero profile", "[phase2]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    std::vector<float> silence(8192, 0.0f);
    denoiser.startLearning();
    denoiser.learnFromBlock(silence.data(), static_cast<int>(silence.size()));
    denoiser.stopLearning();

    auto& profile = denoiser.getNoiseProfile();
    REQUIRE(profile.size() == static_cast<size_t>(SpectralGateDenoiser::kNumBins));

    for (size_t i = 0; i < profile.size(); ++i)
        REQUIRE(profile[i] == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("Learning accumulates noise profile from white noise", "[phase2]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    auto noise = generateWhiteNoise(32768, 0.5f);
    denoiser.startLearning();
    denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
    denoiser.stopLearning();

    auto& profile = denoiser.getNoiseProfile();
    REQUIRE(profile.size() == static_cast<size_t>(SpectralGateDenoiser::kNumBins));

    // White noise should have non-zero energy across all bins
    for (size_t i = 1; i < profile.size() - 1; ++i)
        REQUIRE(profile[i] > 0.0f);
}

TEST_CASE("Multiple learning sessions reset accumulator", "[phase2]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    // First session: loud noise
    auto loudNoise = generateWhiteNoise(8192, 1.0f, 1);
    denoiser.startLearning();
    denoiser.learnFromBlock(loudNoise.data(), static_cast<int>(loudNoise.size()));
    denoiser.stopLearning();

    // Second session: silence
    std::vector<float> silence(8192, 0.0f);
    denoiser.startLearning();
    denoiser.learnFromBlock(silence.data(), static_cast<int>(silence.size()));
    denoiser.stopLearning();

    auto& profile = denoiser.getNoiseProfile();
    for (size_t i = 0; i < profile.size(); ++i)
        REQUIRE(profile[i] == Catch::Approx(0.0f).margin(1e-6f));
}

// ============================================================
// Phase 3: Spectral Gating
// ============================================================

TEST_CASE("Gating attenuates signal below noise floor", "[phase3]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);
    denoiser.setThreshold(1.0f);
    denoiser.setReduction(-40.0f);

    // Learn noise profile from moderate noise
    auto noise = generateWhiteNoise(32768, 0.3f);
    denoiser.startLearning();
    denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
    denoiser.stopLearning();

    // Process a quiet signal (well below learned noise floor)
    const int numSamples = 16384;
    auto quiet = generateWhiteNoise(numSamples, 0.01f, 99);
    auto original = quiet;

    denoiser.processMono(quiet.data(), numSamples);

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);

    // Should be attenuated by at least 20 dB
    float ratioDb = 20.0f * std::log10(outputRMS / (inputRMS + 1e-10f));
    REQUIRE(ratioDb < -20.0f);
}

TEST_CASE("Gating preserves signal above noise floor", "[phase3]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);
    denoiser.setThreshold(1.0f);
    denoiser.setReduction(-40.0f);

    // Learn a quiet noise profile
    auto noise = generateWhiteNoise(32768, 0.01f);
    denoiser.startLearning();
    denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
    denoiser.stopLearning();

    // Process a loud sine (well above noise floor)
    const int numSamples = 16384;
    auto loud = generateSine(1000.0f, 44100.0f, numSamples, 0.8f);
    auto original = loud;

    denoiser.processMono(loud.data(), numSamples);

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(loud.data() + skip, numSamples - skip);

    // Should be within 2 dB
    float ratioDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(ratioDb == Catch::Approx(0.0f).margin(2.0f));
}

TEST_CASE("Threshold parameter controls gating aggressiveness", "[phase3]")
{
    auto runWithThreshold = [](float threshold) -> float {
        SpectralGateDenoiser denoiser;
        denoiser.prepare(44100.0, 512);
        denoiser.setThreshold(threshold);
        denoiser.setReduction(-40.0f);

        auto noise = generateWhiteNoise(32768, 0.1f);
        denoiser.startLearning();
        denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
        denoiser.stopLearning();

        // Use broadband signal so gating affects many bins across thresholds
        const int numSamples = 16384;
        auto signal = generateWhiteNoise(numSamples, 0.15f, 200);

        denoiser.processMono(signal.data(), numSamples);

        const int skip = SpectralGateDenoiser::kFFTSize;
        return computeRMS(signal.data() + skip, numSamples - skip);
    };

    float rmsLowThreshold = runWithThreshold(1.0f);
    float rmsHighThreshold = runWithThreshold(5.0f);

    // Higher threshold should attenuate more
    REQUIRE(rmsHighThreshold < rmsLowThreshold);
}

TEST_CASE("Reduction parameter controls attenuation depth", "[phase3]")
{
    auto runWithReduction = [](float reductionDb) -> float {
        SpectralGateDenoiser denoiser;
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

        const int skip = SpectralGateDenoiser::kFFTSize;
        return computeRMS(signal.data() + skip, numSamples - skip);
    };

    float rmsShallowReduction = runWithReduction(-6.0f);
    float rmsDeepReduction = runWithReduction(-40.0f);

    // Deeper reduction should produce lower RMS
    REQUIRE(rmsDeepReduction < rmsShallowReduction);
}

// ============================================================
// Phase 4: Overlap-Add Continuity
// ============================================================

TEST_CASE("Processing multiple small blocks matches one large block", "[phase4]")
{
    const int totalSamples = 8192;
    auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

    // Process as one large block
    SpectralGateDenoiser denoiser1;
    denoiser1.prepare(44100.0, totalSamples);
    auto large = signal;
    denoiser1.processMono(large.data(), totalSamples);

    // Process as many small blocks
    SpectralGateDenoiser denoiser2;
    denoiser2.prepare(44100.0, 128);
    auto small = signal;
    for (int i = 0; i < totalSamples; i += 128)
        denoiser2.processMono(small.data() + i, 128);

    // Compare steady-state region
    const int skip = SpectralGateDenoiser::kFFTSize;
    for (int i = skip; i < totalSamples; ++i)
        REQUIRE(small[i] == Catch::Approx(large[i]).margin(1e-5f));
}

TEST_CASE("No clicks or discontinuities at block boundaries", "[phase4]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 256);

    const int blockSize = 256;
    const int numBlocks = 32;
    const int totalSamples = blockSize * numBlocks;
    auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

    // Process in blocks
    for (int i = 0; i < totalSamples; i += blockSize)
        denoiser.processMono(signal.data() + i, blockSize);

    // Check for discontinuities - the diff between consecutive samples
    // should never exceed a reasonable threshold for a smooth sine
    const int skip = SpectralGateDenoiser::kFFTSize;
    float maxJump = 0.0f;
    for (int i = skip + 1; i < totalSamples; ++i)
    {
        float jump = std::abs(signal[i] - signal[i - 1]);
        maxJump = std::max(maxJump, jump);
    }

    // For a 440 Hz sine at 44100 Hz, max expected delta is about 2*pi*440/44100 * 0.5 ≈ 0.031
    // Allow some headroom but catch obvious clicks
    REQUIRE(maxJump < 0.1f);
}

// ============================================================
// Phase 5: Reset
// ============================================================

TEST_CASE("prepare and reset return to clean state", "[phase5]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    // Learn noise and process some audio
    auto noise = generateWhiteNoise(8192, 0.5f);
    denoiser.startLearning();
    denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
    denoiser.stopLearning();

    auto signal = generateSine(440.0f, 44100.0f, 4096, 0.5f);
    denoiser.processMono(signal.data(), 4096);

    // Reset
    denoiser.reset();

    // Noise profile should be cleared
    auto& profile = denoiser.getNoiseProfile();
    for (size_t i = 0; i < profile.size(); ++i)
        REQUIRE(profile[i] == Catch::Approx(0.0f).margin(1e-6f));

    // Should pass through cleanly again
    const int numSamples = 16384;
    auto fresh = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
    auto original = fresh;
    denoiser.processMono(fresh.data(), numSamples);

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(fresh.data() + skip, numSamples - skip);
    float ratioDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(ratioDb == Catch::Approx(0.0f).margin(1.0f));
}

// ============================================================
// Phase 6: Performance Optimizations
// ============================================================

TEST_CASE("50% overlap: silence in produces silence out", "[perf]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    std::vector<float> silence(4096, 0.0f);
    denoiser.processMono(silence.data(), static_cast<int>(silence.size()));

    for (int i = 0; i < static_cast<int>(silence.size()); ++i)
        REQUIRE(silence[i] == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("50% overlap: pass-through preserves signal", "[perf]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    const int numSamples = 16384;
    auto input = generateSine(440.0f, 44100.0f, numSamples, 0.5f);
    auto original = input;

    denoiser.processMono(input.data(), numSamples);

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(input.data() + skip, numSamples - skip);

    float ratioDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(ratioDb == Catch::Approx(0.0f).margin(1.0f));
}

TEST_CASE("50% overlap: gating still attenuates below noise floor", "[perf]")
{
    SpectralGateDenoiser denoiser;
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

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(quiet.data() + skip, numSamples - skip);

    float ratioDb = 20.0f * std::log10(outputRMS / (inputRMS + 1e-10f));
    REQUIRE(ratioDb < -20.0f);
}

TEST_CASE("50% overlap: gating still preserves signal above noise floor", "[perf]")
{
    SpectralGateDenoiser denoiser;
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

    const int skip = SpectralGateDenoiser::kFFTSize;
    float inputRMS = computeRMS(original.data() + skip, numSamples - skip);
    float outputRMS = computeRMS(loud.data() + skip, numSamples - skip);

    float ratioDb = 20.0f * std::log10(outputRMS / inputRMS);
    REQUIRE(ratioDb == Catch::Approx(0.0f).margin(2.0f));
}

TEST_CASE("50% overlap: small blocks match large block", "[perf]")
{
    const int totalSamples = 8192;
    auto signal = generateSine(440.0f, 44100.0f, totalSamples, 0.5f);

    SpectralGateDenoiser denoiser1;
    denoiser1.prepare(44100.0, totalSamples);
    auto large = signal;
    denoiser1.processMono(large.data(), totalSamples);

    SpectralGateDenoiser denoiser2;
    denoiser2.prepare(44100.0, 128);
    auto small = signal;
    for (int i = 0; i < totalSamples; i += 128)
        denoiser2.processMono(small.data() + i, 128);

    const int skip = SpectralGateDenoiser::kFFTSize;
    for (int i = skip; i < totalSamples; ++i)
        REQUIRE(small[i] == Catch::Approx(large[i]).margin(1e-5f));
}

TEST_CASE("Performance: processMono throughput", "[perf][!benchmark]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);
    denoiser.setThreshold(1.5f);
    denoiser.setReduction(-30.0f);

    auto noise = generateWhiteNoise(32768, 0.3f);
    denoiser.startLearning();
    denoiser.learnFromBlock(noise.data(), static_cast<int>(noise.size()));
    denoiser.stopLearning();

    auto signal = generateWhiteNoise(44100, 0.5f, 123);

    BENCHMARK("process 1 second of audio")
    {
        denoiser.processMono(signal.data(), 44100);
        return signal[0];
    };
}

// ============================================================
// Phase 7: Processing Time Measurement
// ============================================================

TEST_CASE("getLastProcessingTimeMs returns zero before any processing", "[latency]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    REQUIRE(denoiser.getLastProcessingTimeMs() == Catch::Approx(0.0f));
}

TEST_CASE("getLastProcessingTimeMs returns positive value after processing", "[latency]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    auto signal = generateWhiteNoise(4096, 0.5f);
    denoiser.processMono(signal.data(), static_cast<int>(signal.size()));

    REQUIRE(denoiser.getLastProcessingTimeMs() > 0.0f);
}

TEST_CASE("getLastProcessingTimeMs updates each call", "[latency]")
{
    SpectralGateDenoiser denoiser;
    denoiser.prepare(44100.0, 512);

    // Small block
    auto small = generateWhiteNoise(64, 0.5f);
    denoiser.processMono(small.data(), static_cast<int>(small.size()));
    float timeSmall = denoiser.getLastProcessingTimeMs();
    REQUIRE(timeSmall > 0.0f);

    // Larger block — should take longer (or at least update)
    auto large = generateWhiteNoise(44100, 0.5f);
    denoiser.processMono(large.data(), static_cast<int>(large.size()));
    float timeLarge = denoiser.getLastProcessingTimeMs();
    REQUIRE(timeLarge > 0.0f);

    // The large block should take more time than the small one
    REQUIRE(timeLarge > timeSmall);
}

TEST_CASE("Performance: must process 1s of audio in under 50ms", "[perf]")
{
    SpectralGateDenoiser denoiser;
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

    // Timed run
    auto start = std::chrono::high_resolution_clock::now();
    denoiser.processMono(signal.data(), 44100);
    auto end = std::chrono::high_resolution_clock::now();

    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double ms = static_cast<double>(us) / 1000.0;

    // Must be well under real-time (1s of audio in <50ms = 20x real-time)
    REQUIRE(ms < 50.0);
}
