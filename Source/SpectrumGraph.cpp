#include "SpectrumGraph.h"
#include <algorithm>
#include <cmath>

void SpectrumGraph::setSnapshot(const std::vector<float>& noise,
                                const std::vector<float>& input)
{
    noise_ = noise;

    // Exponential smoothing on the live input curve so it reads as a
    // smoothly-morphing shape rather than a jitter of bar heights.
    if (inputSmoothed_.size() != input.size())
    {
        inputSmoothed_.assign(input.size(), 0.0f);
    }
    const float alpha = 0.35f;
    for (size_t i = 0; i < input.size(); ++i)
        inputSmoothed_[i] = alpha * input[i] + (1.0f - alpha) * inputSmoothed_[i];

    repaint();
}

float SpectrumGraph::freqToX(float freq, float width) const
{
    freq = juce::jlimit(kFreqMin, kFreqMax, freq);
    const float logMin = std::log10(kFreqMin);
    const float logMax = std::log10(kFreqMax);
    const float t = (std::log10(freq) - logMin) / (logMax - logMin);
    return t * width;
}

float SpectrumGraph::magToY(float mag, float height) const
{
    // Avoid log(0); map magnitudes at or below the noise floor to the bottom.
    const float floorMag = magRef() * std::pow(10.0f, kMinDb * 0.05f); // -80 dB
    mag = juce::jmax(mag, floorMag);
    const float db = 20.0f * std::log10(mag / magRef());
    const float clamped = juce::jlimit(kMinDb, kMaxDb, db);
    const float t = (clamped - kMinDb) / (kMaxDb - kMinDb); // 0 at bottom, 1 at top
    return height - t * height;
}

void SpectrumGraph::buildCurve(juce::Path& out,
                               const std::vector<float>& mags,
                               float scale,
                               juce::Rectangle<float> area) const
{
    if (mags.empty())
        return;

    const float w = area.getWidth();
    const float h = area.getHeight();

    bool started = false;
    const int N = static_cast<int>(mags.size());

    for (int bin = 1; bin < N; ++bin) // skip DC
    {
        const float f = binToFreq(bin);
        if (f < kFreqMin) continue;
        if (f > kFreqMax) break;

        const float x = area.getX() + freqToX(f, w);
        const float y = area.getY() + magToY(mags[static_cast<size_t>(bin)] * scale, h);

        if (! started)
        {
            out.startNewSubPath(x, y);
            started = true;
        }
        else
        {
            out.lineTo(x, y);
        }
    }
}

void SpectrumGraph::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(2.0f);

    // Soft grid — horizontal dB lines at every 20 dB.
    {
        g.setColour(KnobDesign::accentColour.withAlpha(0.12f));
        for (float db = kMinDb; db <= kMaxDb; db += 20.0f)
        {
            const float floorMag = magRef() * std::pow(10.0f, db * 0.05f);
            const float y = bounds.getY() + magToY(floorMag, bounds.getHeight());
            g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
        }

        // Vertical grid lines at 100 Hz, 1 kHz, 10 kHz.
        const float decades[] = { 100.0f, 1000.0f, 10000.0f };
        for (float f : decades)
        {
            const float x = bounds.getX() + freqToX(f, bounds.getWidth());
            g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
        }
    }

    // Noise profile — darker, thicker. Uses the darker accent so it reads as
    // the baseline the gate was calibrated against.
    if (! noise_.empty())
    {
        juce::Path p;
        buildCurve(p, noise_, 1.0f, bounds);
        g.setColour(KnobDesign::accentColour.darker(0.2f));
        g.strokePath(p, juce::PathStrokeType(2.5f));
    }

    // Threshold — noise profile scaled by the current threshold knob.
    // Medium brightness / medium thickness so it sits between the two others.
    if (! noise_.empty())
    {
        juce::Path p;
        buildCurve(p, noise_, thresholdMult_, bounds);
        g.setColour(KnobDesign::accentColour);
        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

    // Live input — brightest, thinnest.
    if (! inputSmoothed_.empty())
    {
        juce::Path p;
        buildCurve(p, inputSmoothed_, 1.0f, bounds);
        g.setColour(KnobDesign::accentHoverColour);
        g.strokePath(p, juce::PathStrokeType(1.0f));
    }
}
