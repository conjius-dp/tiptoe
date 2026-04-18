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

    const int N = static_cast<int>(mags.size());

    // Locate the first and last bins whose frequency falls inside the
    // displayed range so we can anchor the curve at the graph's LEFT and
    // RIGHT edges — otherwise bin 1 lands ~1 % inset from the left, making
    // the curve look shorter on that side than on the right.
    int firstIn = -1;
    int lastIn  = -1;
    for (int bin = 1; bin < N; ++bin)
    {
        const float f = binToFreq(bin);
        if (f < kFreqMin) continue;
        if (f > kFreqMax) break;
        if (firstIn < 0) firstIn = bin;
        lastIn = bin;
    }
    if (firstIn < 0)
        return;

    const float yFirst = area.getY() + magToY(mags[static_cast<size_t>(firstIn)] * scale, h);
    out.startNewSubPath(area.getX(), yFirst);

    for (int bin = firstIn; bin <= lastIn; ++bin)
    {
        const float f = binToFreq(bin);
        const float x = area.getX() + freqToX(f, w);
        const float y = area.getY() + magToY(mags[static_cast<size_t>(bin)] * scale, h);
        out.lineTo(x, y);
    }

    // Extend to the right edge with the last bin's value so the curve
    // touches the right border symmetrically with the left.
    const float yLast = area.getY() + magToY(mags[static_cast<size_t>(lastIn)] * scale, h);
    out.lineTo(area.getRight(), yLast);
}

void SpectrumGraph::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Soft grid — horizontal dB lines at every 20 dB.
    {
        g.setColour(KnobDesign::accentColour.withAlpha(0.12f));
        for (float db = kMinDb; db <= kMaxDb; db += 20.0f)
        {
            const float floorMag = magRef() * std::pow(10.0f, db * 0.05f);
            const float y = bounds.getY() + magToY(floorMag, bounds.getHeight());
            g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
        }

        // Vertical grid: full logarithmic set — every 1-2-3-...-9 multiplier
        // within each decade, with decade boundaries (100, 1 k, 10 k) drawn
        // slightly brighter so the eye can latch onto them. Minor lines at
        // the intra-decade multipliers give the spectrum its "log paper" feel
        // at a glance.
        const float decadeStarts[] = { 10.0f, 100.0f, 1000.0f, 10000.0f };
        for (float base : decadeStarts)
        {
            for (int m = 1; m <= 9; ++m)
            {
                const float f = base * static_cast<float>(m);
                if (f < kFreqMin || f > kFreqMax) continue;
                const bool isDecade = (m == 1);
                g.setColour(KnobDesign::accentColour
                                .withAlpha(isDecade ? 0.18f : 0.08f));
                const float x = bounds.getX() + freqToX(f, bounds.getWidth());
                g.drawLine(x, bounds.getY(), x, bounds.getBottom(), 1.0f);
            }
        }
    }

    // Rounded stroke style — same CPU cost as a plain stroke (JUCE computes
    // join geometry once at stroke time) but turns the polyline jaggies into
    // a softer, continuous curve. No per-point interpolation.
    const juce::PathStrokeType rounded25 {
        2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded };
    const juce::PathStrokeType rounded15 {
        1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded };
    const juce::PathStrokeType rounded10 {
        1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded };

    // Noise profile — darker, thicker.
    if (! noise_.empty())
    {
        juce::Path p;
        buildCurve(p, noise_, 1.0f, bounds);
        g.setColour(KnobDesign::accentColour.darker(0.2f));
        g.strokePath(p, rounded25);
    }

    // Sensitivity — noise profile scaled by the current sensitivity knob.
    if (! noise_.empty())
    {
        juce::Path p;
        buildCurve(p, noise_, sensitivityMult_, bounds);
        g.setColour(KnobDesign::accentColour);
        g.strokePath(p, rounded15);
    }

    // Live input — brightest, thinnest.
    if (! inputSmoothed_.empty())
    {
        juce::Path p;
        buildCurve(p, inputSmoothed_, 1.0f, bounds);
        g.setColour(KnobDesign::accentHoverColour);
        g.strokePath(p, rounded10);
    }

    // Frequency labels — plain numbers, no ticks, each centred horizontally
    // on its grid line and anchored to the bottom of the graph so they sit
    // on top of the grid but don't interfere with the curves above them.
    {
        g.setColour(KnobDesign::accentColour.withAlpha(0.55f));
        g.setFont(juce::FontOptions(10.0f));
        const float labels[] = { 50.0f, 200.0f, 500.0f, 1000.0f,
                                 5000.0f, 10000.0f, 15000.0f };
        const int labelW = 36;
        const int labelH = 12;
        for (float f : labels)
        {
            const float x = bounds.getX() + freqToX(f, bounds.getWidth());
            const juce::String txt = (f >= 1000.0f)
                ? (juce::String(f / 1000.0f, (std::fmod(f, 1000.0f) == 0.0f) ? 0 : 1) + "k")
                : juce::String(static_cast<int>(f));
            const juce::Rectangle<int> r(
                static_cast<int>(x) - labelW / 2,
                static_cast<int>(bounds.getBottom()) - labelH - 1,
                labelW, labelH);
            g.drawText(txt, r, juce::Justification::centred, false);
        }
    }
}
