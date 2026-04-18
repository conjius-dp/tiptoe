#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "KnobDesign.h"

// Visualisation of the spectral gate state:
//  * learned noise profile — darker orange, thicker stroke
//  * sensitivity curve (noise * sensitivityMultiplier) — mid orange, medium stroke
//  * live input magnitude — bright orange, thin stroke
//
// X axis is logarithmic from 20 Hz to 20 kHz. Y axis is dB (magRef = FFT/4
// → 0 dB; floor at -80 dB). The component re-paints whenever setSnapshot()
// receives new data, so the editor timer is the only driver.
class SpectrumGraph : public juce::Component
{
public:
    SpectrumGraph() = default;

    void setSampleRate(double sr) { sampleRate_ = sr; }
    void setFftSize(int fftSize) { fftSize_ = fftSize; }

    void setSensitivityMultiplier(float mult) { sensitivityMult_ = mult; }

    // Called from the editor's timer. `noise` is the learned noise profile
    // (may be empty before learning). `input` is the latest live magnitude
    // snapshot. Internal smoothing is applied to the input curve only.
    void setSnapshot(const std::vector<float>& noise,
                     const std::vector<float>& input);

    void paint(juce::Graphics& g) override;

private:
    double sampleRate_    = 44100.0;
    int    fftSize_       = 2048;
    float  sensitivityMult_ = 1.5f;

    std::vector<float> noise_;          // copied from DSP
    std::vector<float> inputSmoothed_;  // exponentially smoothed

    // dB range for the Y axis.
    static constexpr float kMinDb = -80.0f;
    static constexpr float kMaxDb = 0.0f;

    // Frequency range (always shown, regardless of sample rate).
    static constexpr float kFreqMin = 20.0f;
    static constexpr float kFreqMax = 20000.0f;

    // Magnitude reference so 0 dB = full-scale sine FFT bin magnitude.
    float magRef() const { return static_cast<float>(fftSize_) * 0.25f; }

    float binToFreq(int bin) const
    {
        return static_cast<float>(bin) * static_cast<float>(sampleRate_)
             / static_cast<float>(fftSize_);
    }

    float freqToX(float freq, float width) const;
    float magToY(float mag, float height) const;

    void buildCurve(juce::Path& out,
                    const std::vector<float>& mags,
                    float scale,
                    juce::Rectangle<float> area) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumGraph)
};
