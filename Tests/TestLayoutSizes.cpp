// Layout-math tests.
//
// TDD guard-rails for the knob-column label font sizing / positioning
// relative to the LEARN button text and the START/STOP button text.
// All values live in Source/LayoutSizes.h as pure-arithmetic constexpr
// helpers so the editor and these tests both read from the same formulas.

#include <juce_core/juce_core.h>

#include "LayoutSizes.h"

// Reference values from before the change, hard-coded here so the tests
// document the *intent* of the re-sizing — column labels must go UP,
// LEARN text and button text must go DOWN.
namespace OldSizes
{
    // Pre-change: "column label font was w * 0.024, button was w * 0.030,
    // learn label multiplier was 0.65 of (diameter * labelFontScale)."
    inline constexpr float columnLabelFontSize(float w) { return w * 0.024f; }
    inline constexpr float learnButtonFontSize(float w) { return w * 0.030f; }
    inline constexpr float learnTextMultiplier = 0.65f;
}

class LayoutSizesTests : public juce::UnitTest
{
public:
    LayoutSizesTests() : juce::UnitTest("LayoutSizes") {}

    void runTest() override
    {
        constexpr float kDefaultW = 650.0f;
        constexpr float kDefaultH = 540.0f;

        beginTest("Column label font is bigger than it was, and at least as big as the old LEARN button size");
        {
            const float newSize = KnobDesign::columnLabelFontSize(kDefaultW);
            const float oldSize = OldSizes::columnLabelFontSize(kDefaultW);
            expect(newSize > oldSize,
                   "column label font size did not grow");
            // Must be at least as big as the old LEARN button size. Upper
            // bound keeps future tweaks from ballooning the label.
            expect(newSize >= OldSizes::learnButtonFontSize(kDefaultW) - 0.01f,
                   "column label font fell below the old LEARN button size");
            expect(newSize < OldSizes::learnButtonFontSize(kDefaultW) * 1.25f,
                   "column label font grew more than 25% over the old LEARN size");
        }

        beginTest("LEARN button font shrunk below its previous size");
        {
            const float newSize = KnobDesign::learnButtonFontSize(kDefaultW);
            const float oldSize = OldSizes::learnButtonFontSize(kDefaultW);
            expect(newSize < oldSize,
                   "learn button font size did not shrink");
        }

        beginTest("LEARN text multiplier shrunk below its previous value");
        {
            const float newMult = KnobDesign::learnTextFontScaleFactor();
            expect(newMult < OldSizes::learnTextMultiplier,
                   "learn text multiplier did not shrink");
        }

        beginTest("Column label font is now strictly bigger than the LEARN button font");
        {
            expect(KnobDesign::columnLabelFontSize(kDefaultW)
                   > KnobDesign::learnButtonFontSize(kDefaultW),
                   "column label font must now be bigger than the button text");
        }

        beginTest("Column labels slid down but don't encroach on the Reduction -30 tick label area");
        {
            const float labelBottom = KnobDesign::columnLabelBottomY(kDefaultW, kDefaultH);
            const float minusThirtyTop = KnobDesign::reductionMidTickLabelRectTopY(kDefaultW, kDefaultH);

            expect(labelBottom < minusThirtyTop,
                   "column label bottom overlaps the -30 tick label rect");

            // Small breathing margin on top of non-overlap, so small future
            // reflow tweaks don't accidentally tighten to zero.
            expect((minusThirtyTop - labelBottom) >= 3.0f,
                   "column label bottom sits within 3 px of the -30 tick label");

            // Column labels now sit near the TOP of the knob-area sub-window
            // because a fixed kKnobAreaTopPadPx block sits above them giving
            // the breathing room previously provided by a 0.04+ relative
            // offset. So we accept a small fraction (≤ 0.05) here.
            expect(KnobDesign::columnLabelTopYInKnobArea() < 0.05f,
                   "column label position expected to be near the top of "
                   "the knob-area sub-window (the top-pad provides the gap)");
        }
    }
};

static LayoutSizesTests layoutSizesTests;
