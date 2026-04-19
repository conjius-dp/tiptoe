#pragma once

#include "ResamplerKernel.h"
#include <juce_core/juce_core.h>
#include <vector>

// M-to-1 FIR decimator. Convolves the input with an anti-alias low-pass
// then keeps every D-th output sample. Ring-buffer history so process()
// never allocates — the state array sized at prepare() holds the most
// recent (tapCount − 1) samples.
//
// Latency (in input-rate samples) is (tapCount − 1) / 2 — the FIR's
// group delay. process() takes `numInputSamples`, writes
// `numInputSamples / D` decimated samples, and returns how many it wrote.
class Decimator
{
public:
    Decimator() = default;

    void prepare(double /*sampleRate*/, int /*maxBlockSize*/, int decimationFactor)
    {
        D_ = juce::jmax(2, decimationFactor);
        taps_ = ResamplerKernel::design(D_);
        state_.assign(taps_.size(), 0.0f);
        writePos_ = 0;
        sampleCounter_ = 0;
    }

    void reset() noexcept
    {
        std::fill(state_.begin(), state_.end(), 0.0f);
        writePos_ = 0;
        sampleCounter_ = 0;
    }

    // Latency this decimator contributes, in INPUT-rate samples. Equal to
    // the FIR's linear-phase group delay.
    int getLatencyInputSamples() const noexcept
    {
        return (static_cast<int>(taps_.size()) - 1) / 2;
    }

    // Writes `numInputSamples / D_` samples to `out`, returns that count.
    int process(const float* in, float* out, int numInputSamples) noexcept
    {
        const int   N        = static_cast<int>(taps_.size());
        const int   D        = D_;
        float* const state   = state_.data();
        const float* tapData = taps_.data();
        int   wp             = writePos_;
        int   counter        = sampleCounter_;
        int   outWritten     = 0;

        for (int i = 0; i < numInputSamples; ++i)
        {
            // Push newest sample into the ring buffer.
            state[wp] = in[i];
            wp = (wp + 1) % N;

            ++counter;
            if (counter < D) continue;
            counter = 0;

            // Convolve: walk backwards from the newest sample. The ring
            // contains samples at positions (wp-1, wp-2, ..., wp-N+1)
            // modulo N, where wp-1 is the newest.
            float acc = 0.0f;
            int   idx = wp - 1;
            if (idx < 0) idx += N;
            for (int k = 0; k < N; ++k)
            {
                acc += state[idx] * tapData[k];
                --idx;
                if (idx < 0) idx += N;
            }
            out[outWritten++] = acc;
        }

        writePos_      = wp;
        sampleCounter_ = counter;
        return outWritten;
    }

private:
    int   D_ = 2;
    int   writePos_ = 0;
    int   sampleCounter_ = 0;
    std::vector<float> taps_;
    std::vector<float> state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Decimator)
};
