#include "SpectrumGraph.h"
#include <algorithm>
#include <cmath>

void SpectrumGraph::setSnapshot(const std::vector<float>& noise,
                                const std::vector<float>& input)
{
    noise_ = noise;

    // Exponential smoothing over time on the live input curve so it reads as
    // a smoothly-morphing shape rather than a jitter of bar heights. Lower
    // alpha → slower, silkier transitions between successive snapshots.
    if (inputSmoothed_.size() != input.size())
        inputSmoothed_.assign(input.size(), 0.0f);

    const float alpha = 0.18f;
    for (size_t i = 0; i < input.size(); ++i)
        inputSmoothed_[i] = alpha * input[i] + (1.0f - alpha) * inputSmoothed_[i];

    repaint();
}

// 5-bin gaussian-ish blur across frequency bins. Cheap and preserves peak
// locations while taking the sharp edges off adjacent-bin jitter.
static void smoothBins(const std::vector<float>& src, std::vector<float>& dst)
{
    const int N = static_cast<int>(src.size());
    dst.resize(static_cast<size_t>(N));
    if (N == 0) return;

    // Weights: [1, 4, 6, 4, 1] / 16 — a binomial approximation of a Gaussian.
    for (int i = 0; i < N; ++i)
    {
        const int im2 = std::max(0, i - 2);
        const int im1 = std::max(0, i - 1);
        const int ip1 = std::min(N - 1, i + 1);
        const int ip2 = std::min(N - 1, i + 2);
        dst[static_cast<size_t>(i)] =
            (src[static_cast<size_t>(im2)]
             + 4.0f * src[static_cast<size_t>(im1)]
             + 6.0f * src[static_cast<size_t>(i)]
             + 4.0f * src[static_cast<size_t>(ip1)]
             + src[static_cast<size_t>(ip2)])
            * (1.0f / 16.0f);
    }
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

    // Quadratic-midpoint smoothing: walk the bins and, instead of drawing a
    // straight segment to each point, draw a quadraticTo that treats the
    // current bin as a *control* point and the midpoint between it and the
    // next bin as the curve's on-path anchor. That turns the polyline into a
    // continuous C¹ curve with no per-point interpolation cost — one
    // quadraticTo per bin regardless of density.
    const float yFirst = area.getY() + magToY(mags[static_cast<size_t>(firstIn)] * scale, h);
    out.startNewSubPath(area.getX(), yFirst);

    auto xyAt = [&](int bin) -> juce::Point<float> {
        const float f = binToFreq(bin);
        return { area.getX() + freqToX(f, w),
                 area.getY() + magToY(mags[static_cast<size_t>(bin)] * scale, h) };
    };

    auto prev = xyAt(firstIn);
    out.lineTo(prev);

    for (int bin = firstIn + 1; bin <= lastIn; ++bin)
    {
        const auto curr = xyAt(bin);
        const juce::Point<float> mid { (prev.x + curr.x) * 0.5f,
                                       (prev.y + curr.y) * 0.5f };
        out.quadraticTo(prev, mid);
        prev = curr;
    }

    // Extend to the right edge, also smoothed — quadraticTo from the last
    // bin as the control point and the right-edge point as the on-path end.
    const juce::Point<float> rightEnd { area.getRight(), prev.y };
    out.quadraticTo(prev, rightEnd);
}

void SpectrumGraph::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Rounded-corner masking is done at the END of paint() by filling the
    // triangular slivers between the bounds corner and the rounded-border
    // arc with the editor's bg colour. That's more reliable than
    // reduceClipRegion(Path) — which rasterises to a RectangleList and can
    // leave sub-pixel slivers along the curve.

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

    // Bin-space gaussian-ish smoothing before drawing — rounds off the
    // single-bin spikes that otherwise make the curve look jagged. Runs
    // into a reused scratch so the cost is one pass per visible curve.
    std::vector<float> smoothScratch;

    // Noise profile — darker, thicker.
    if (! noise_.empty())
    {
        smoothBins(noise_, smoothScratch);
        juce::Path p;
        buildCurve(p, smoothScratch, 1.0f, bounds);
        g.setColour(KnobDesign::accentColour.darker(0.2f));
        g.strokePath(p, rounded25);
    }

    // Sensitivity — noise profile scaled by the current sensitivity knob.
    if (! noise_.empty())
    {
        smoothBins(noise_, smoothScratch);
        juce::Path p;
        buildCurve(p, smoothScratch, sensitivityMult_, bounds);
        g.setColour(KnobDesign::accentColour);
        g.strokePath(p, rounded15);
    }

    // Live input — brightest, thinnest.
    if (! inputSmoothed_.empty())
    {
        smoothBins(inputSmoothed_, smoothScratch);
        juce::Path p;
        buildCurve(p, smoothScratch, 1.0f, bounds);
        g.setColour(KnobDesign::accentHoverColour);
        g.strokePath(p, rounded10);
    }

    // Frequency labels — plain numbers, no ticks, each centred horizontally
    // on its grid line and anchored to the bottom of the graph so they sit
    // on top of the grid but don't interfere with the curves above them.
    {
        // Font size scales with window width so the labels keep the same
        // visual weight as the editor is resized. Baseline 14 px at the
        // default editor width.
        const float fontSize = 14.0f
            * (bounds.getWidth() / static_cast<float>(KnobDesign::defaultWidth));
        g.setColour(KnobDesign::accentColour.withAlpha(0.55f));
        g.setFont(juce::FontOptions(fontSize));
        const float labels[] = { 50.0f, 200.0f, 500.0f, 1000.0f,
                                 5000.0f, 10000.0f, 15000.0f };
        const int labelW = static_cast<int>(fontSize * 3.6f);
        const int labelH = static_cast<int>(fontSize * 1.2f);
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

    // Corner masks — belt-and-braces. Paint the triangular slivers
    // OUTSIDE the rounded curve (between the curve and the component's
    // rectangular bounds) with the editor's bg colour, so any strokes or
    // labels that leaked past the path-based clip get overpainted. The
    // orange border (drawn later by paintOverChildren) sits on top of
    // these masks.
    if (cornerRadius_ > 0.5f)
    {
        const float w = bounds.getWidth();
        const float h = bounds.getHeight();
        const float r = juce::jmin(cornerRadius_, juce::jmin(w, h) * 0.5f);
        const float L = bounds.getX();
        const float T = bounds.getY();
        const float R = bounds.getRight();

        // Top-left sliver: square corner (L, T)–(L+r, T+r) minus the
        // quarter-disc anchored at (L+r, T+r). The curve below is the same
        // one used by the clip path above — same endpoints, same control
        // point — so the two paint against each other exactly.
        juce::Path topLeft;
        topLeft.startNewSubPath(L,     T);
        topLeft.lineTo        (L + r, T);
        topLeft.quadraticTo   (L,     T,   L, T + r);
        topLeft.closeSubPath();

        // Top-right sliver (mirror).
        juce::Path topRight;
        topRight.startNewSubPath(R,     T);
        topRight.lineTo        (R,     T + r);
        topRight.quadraticTo   (R,     T,   R - r, T);
        topRight.closeSubPath();

        g.setColour(KnobDesign::bgColour);
        g.fillPath(topLeft);
        g.fillPath(topRight);
    }
}
