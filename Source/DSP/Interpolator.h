#pragma once

#include "ResamplerKernel.h"
#include <juce_core/juce_core.h>
#include <vector>

// 1-to-M FIR interpolator. Zero-stuffs the input (D−1 zeros between each
// sample) then convolves with the anti-image low-pass. Exploits the
// polyphase structure: only 1/D of the FIR taps hit non-zero state on
// each output sample, so the per-output cost is `tapsPerPhase` multiply-
// adds instead of the full `tapsPerPhase × D`.
//
// Kernel gain is scaled by D here so DC and in-band amplitude are
// preserved through the zero-stuffing.
//
// Latency in INPUT-rate samples is the same as the decimator's: the
// FIR's linear-phase group delay divided by D (because we're running at
// the decimated rate on the input side).
class Interpolator
{
public:
    Interpolator() = default;

    void prepare(double /*sampleRate*/, int /*maxInputBlockSize*/, int interpolationFactor)
    {
        D_ = juce::jmax(2, interpolationFactor);
        auto proto = ResamplerKernel::design(D_);
        // Scale up by D to compensate for zero-stuffing energy loss.
        for (auto& v : proto) v *= static_cast<float>(D_);

        // Split into D polyphase branches. Branch p holds taps
        // proto[p], proto[p + D], proto[p + 2D], …
        const int nTaps = static_cast<int>(proto.size());
        tapsPerPhase_ = nTaps / D_;
        phases_.assign(static_cast<size_t>(D_),
                       std::vector<float>(static_cast<size_t>(tapsPerPhase_), 0.0f));
        for (int n = 0; n < nTaps; ++n)
        {
            const int p = n % D_;
            const int k = n / D_;
            phases_[static_cast<size_t>(p)][static_cast<size_t>(k)] =
                proto[static_cast<size_t>(n)];
        }

        state_.assign(static_cast<size_t>(tapsPerPhase_), 0.0f);
        writePos_ = 0;
    }

    void reset() noexcept
    {
        std::fill(state_.begin(), state_.end(), 0.0f);
        writePos_ = 0;
    }

    int getLatencyInputSamples() const noexcept
    {
        // Linear-phase group delay of the prototype (at the output rate)
        // divided by D to express in INPUT-rate samples.
        const int nTaps = tapsPerPhase_ * D_;
        return ((nTaps - 1) / 2) / D_;
    }

    // Writes `numInputSamples * D_` samples to `out`, returns that count.
    int process(const float* in, float* out, int numInputSamples) noexcept
    {
        const int    D       = D_;
        const int    Tp      = tapsPerPhase_;
        float* const state   = state_.data();
        int   wp             = writePos_;
        int   outWritten     = 0;

        for (int i = 0; i < numInputSamples; ++i)
        {
            state[wp] = in[i];
            wp = (wp + 1) % Tp;

            // Emit D output samples by running each polyphase branch
            // against the current state ring. One MAC per tap per phase.
            for (int p = 0; p < D; ++p)
            {
                const float* taps = phases_[static_cast<size_t>(p)].data();
                float acc = 0.0f;
                int   idx = wp - 1;
                if (idx < 0) idx += Tp;
                for (int k = 0; k < Tp; ++k)
                {
                    acc += state[idx] * taps[k];
                    --idx;
                    if (idx < 0) idx += Tp;
                }
                out[outWritten++] = acc;
            }
        }

        writePos_ = wp;
        return outWritten;
    }

private:
    int D_ = 2;
    int tapsPerPhase_ = 0;
    int writePos_ = 0;
    std::vector<std::vector<float>> phases_;
    std::vector<float> state_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Interpolator)
};
