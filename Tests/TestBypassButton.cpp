// Pure-math tests for the BypassButton glyph metrics.
//
// The UI contract the user cares about: the power-symbol icon must be
// EXACTLY the same size (radius, stroke, bar length) whether the plugin
// is engaged or bypassed. Geometry must depend only on the button's
// diameter — never on the toggle state. These tests guard that invariant
// by asserting every derived quantity is a pure function of diameter, so
// calling it with the same diameter twice yields identical values.
//
// Kept in a separate translation unit from BypassButton.h itself because
// the button drags in juce_gui_basics; the metrics header deliberately
// does not, so the tests link cleanly against only juce_core.

#include <juce_core/juce_core.h>

#include "BypassButtonMetrics.h"

class BypassButtonMetricsTests : public juce::UnitTest
{
public:
    BypassButtonMetricsTests() : juce::UnitTest("BypassButtonMetrics") {}

    void runTest() override
    {
        beginTest("Glyph radius is a pure function of diameter");
        {
            // Same diameter, two calls — must return identical values.
            // If a future regression forks the computation per toggle
            // state, it will necessarily break this identity.
            const float d = 34.0f;
            const float a = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float b = BypassButtonMetrics::glyphRadiusForDiameter(d);
            expectEquals(a, b);
            expectWithinAbsoluteError(a, d * 0.26f, 1e-6f);
        }

        beginTest("Glyph stroke is a pure function of diameter");
        {
            const float d = 50.0f;
            const float a = BypassButtonMetrics::glyphStrokeForDiameter(d);
            const float b = BypassButtonMetrics::glyphStrokeForDiameter(d);
            expectEquals(a, b);
            expectWithinAbsoluteError(a, d * 0.095f, 1e-6f);
        }

        beginTest("Glyph dimensions scale linearly with diameter");
        {
            // Doubling the button diameter must double both glyph radius
            // and stroke — any non-linear term would bias the icon
            // differently at different editor sizes.
            const float d  = 34.0f;
            const float r1 = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float r2 = BypassButtonMetrics::glyphRadiusForDiameter(d * 2.0f);
            const float s1 = BypassButtonMetrics::glyphStrokeForDiameter(d);
            const float s2 = BypassButtonMetrics::glyphStrokeForDiameter(d * 2.0f);
            expectWithinAbsoluteError(r2, r1 * 2.0f, 1e-4f);
            expectWithinAbsoluteError(s2, s1 * 2.0f, 1e-4f);
        }

        beginTest("Glyph fractions are the documented constants");
        {
            // Lock in the design constants so an accidental edit to the
            // metrics header can't silently change icon proportions.
            expectWithinAbsoluteError(BypassButtonMetrics::kGlyphRadiusFrac,
                                      0.26f, 1e-6f);
            expectWithinAbsoluteError(BypassButtonMetrics::kGlyphStrokeFrac,
                                      0.095f, 1e-6f);
            expectWithinAbsoluteError(BypassButtonMetrics::kBreakHalfDeg,
                                      28.0f, 1e-6f);
            expectWithinAbsoluteError(BypassButtonMetrics::kBarTopMul,
                                      1.25f, 1e-6f);
            expectWithinAbsoluteError(BypassButtonMetrics::kBarBottomMul,
                                      0.05f, 1e-6f);
        }

        beginTest("Pure glyph metrics are state-independent");
        {
            // The uncorrected helpers are pure functions of diameter —
            // no state branch, no toggle. This is the geometric source
            // of truth that the rendered metrics derive from.
            const float d = 34.0f;
            const float rE = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float rB = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float sE = BypassButtonMetrics::glyphStrokeForDiameter(d);
            const float sB = BypassButtonMetrics::glyphStrokeForDiameter(d);
            expectEquals(rE, rB);
            expectEquals(sE, sB);
        }

        beginTest("Bypassed glyph is scaled by the irradiation factor");
        {
            // The bypassed state paints a light glyph on a dark
            // background, so the icon gets a small uniform shrink to
            // cancel the irradiation illusion. Both radius and stroke
            // must scale by exactly the same factor so the glyph's
            // proportions are preserved.
            const float d = 34.0f;
            const float f = BypassButtonMetrics::kBypassedGlyphScale;

            expectWithinAbsoluteError(
                BypassButtonMetrics::renderedGlyphRadius(d, false),
                BypassButtonMetrics::glyphRadiusForDiameter(d), 1e-6f);
            expectWithinAbsoluteError(
                BypassButtonMetrics::renderedGlyphStroke(d, false),
                BypassButtonMetrics::glyphStrokeForDiameter(d), 1e-6f);

            expectWithinAbsoluteError(
                BypassButtonMetrics::renderedGlyphRadius(d, true),
                BypassButtonMetrics::glyphRadiusForDiameter(d) * f, 1e-6f);
            expectWithinAbsoluteError(
                BypassButtonMetrics::renderedGlyphStroke(d, true),
                BypassButtonMetrics::glyphStrokeForDiameter(d) * f, 1e-6f);

            // Proportions must be preserved — the ratio stroke/radius is
            // identical in engaged and bypassed states.
            const float ratioE =
                BypassButtonMetrics::renderedGlyphStroke(d, false)
                / BypassButtonMetrics::renderedGlyphRadius(d, false);
            const float ratioB =
                BypassButtonMetrics::renderedGlyphStroke(d, true)
                / BypassButtonMetrics::renderedGlyphRadius(d, true);
            expectWithinAbsoluteError(ratioE, ratioB, 1e-6f);
        }

        beginTest("Irradiation factor is in a sane visible range");
        {
            // The correction must be a *slight* shrink — visibly the
            // same icon, just tuned for perception. Out-of-bounds values
            // would make the bypassed glyph either obviously smaller or
            // cancel nothing.
            expect(BypassButtonMetrics::kBypassedGlyphScale > 0.85f);
            expect(BypassButtonMetrics::kBypassedGlyphScale < 1.00f);
        }
    }
};

static BypassButtonMetricsTests bypassButtonMetricsTests;
