#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

// Shared windowed-sinc kernel used by Decimator and Interpolator.
// One design path, one FIR, two users — keeps the anti-alias and
// anti-image filters identical by construction so the decimate→
// interpolate round-trip is magnitude-flat in-band.
//
// The kernel is the `DecimationFactor × base` taps of a Kaiser-windowed
// sinc, normalized so the decimator has unity DC gain and the
// interpolator compensates for the zero-stuffing 1/D energy loss by
// scaling up by D.
namespace ResamplerKernel
{
    // Base taps-per-phase. 8 polyphase phases × 16 taps = 128 total —
    // gives a sharp, -60 dB stopband at passband edge fs/(2D) × 0.9.
    inline constexpr int kTapsPerPhase = 16;

    // Build the full 128-tap low-pass FIR for decimation factor D.
    // Cutoff is placed at 0.9 × Nyquist_of_decimated (fs / (2D)), so a
    // full-rate signal inside that band survives the round-trip.
    inline std::vector<float> design(int decimationFactor)
    {
        const int    D       = juce::jmax(2, decimationFactor);
        const int    nTaps   = kTapsPerPhase * D;
        // Normalised cutoff: 0.9 × (fs / (2D)) / fs = 0.45 / D.
        const double cutoff  = 0.45 / static_cast<double>(D);

        std::vector<float> h(static_cast<size_t>(nTaps), 0.0f);
        const double centre = 0.5 * (nTaps - 1);
        // Kaiser beta ≈ 8 gives about -80 dB sidelobe — more than enough
        // for the 40 dB stopband the tests require. Roll our own I₀
        // (modified Bessel function, first kind, order 0) since the JUCE
        // WindowingFunction API is geared to fixed-length vectors.
        const double beta = 8.0;
        auto i0 = [](double x) {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 25; ++k)
            {
                const double t = x / (2.0 * k);
                term *= t * t;
                sum  += term;
                if (term < 1e-12 * sum) break;
            }
            return sum;
        };
        const double i0b = i0(beta);

        double acc = 0.0;
        for (int n = 0; n < nTaps; ++n)
        {
            const double m = static_cast<double>(n) - centre;
            // Ideal sinc at normalised cutoff.
            const double s = (std::abs(m) < 1e-9)
                           ? (2.0 * cutoff)
                           : std::sin(2.0 * juce::MathConstants<double>::pi * cutoff * m)
                                 / (juce::MathConstants<double>::pi * m);
            // Kaiser window.
            const double r = 2.0 * m / static_cast<double>(nTaps - 1);
            const double w = i0(beta * std::sqrt(juce::jmax(0.0, 1.0 - r * r))) / i0b;
            const double tap = s * w;
            h[static_cast<size_t>(n)] = static_cast<float>(tap);
            acc += tap;
        }

        // Normalize to unity DC gain.
        if (acc > 0.0)
        {
            const float inv = static_cast<float>(1.0 / acc);
            for (auto& v : h) v *= inv;
        }
        return h;
    }

    // Group delay (in samples at the kernel's native rate) of the FIR
    // produced by design(). For linear-phase symmetric FIRs this is
    // exactly (N-1)/2.
    inline int groupDelay(int decimationFactor) noexcept
    {
        const int nTaps = kTapsPerPhase * juce::jmax(2, decimationFactor);
        return (nTaps - 1) / 2;
    }
}
