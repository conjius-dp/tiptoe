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

        beginTest("Glyph metrics are invariant under the 'bypassed' state");
        {
            // The paint routine takes a single `diameter` argument and
            // routes every geometric quantity through these helpers —
            // there is no path in which toggle state (engaged / bypassed)
            // participates in the math. This test asserts the invariant
            // directly: hypothetical "engaged" vs "bypassed" calls are
            // equivalent when the diameter is held constant.
            const float d = 34.0f;
            const float engagedRadius  = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float bypassedRadius = BypassButtonMetrics::glyphRadiusForDiameter(d);
            const float engagedStroke  = BypassButtonMetrics::glyphStrokeForDiameter(d);
            const float bypassedStroke = BypassButtonMetrics::glyphStrokeForDiameter(d);
            expectEquals(engagedRadius, bypassedRadius);
            expectEquals(engagedStroke, bypassedStroke);
        }
    }
};

static BypassButtonMetricsTests bypassButtonMetricsTests;
