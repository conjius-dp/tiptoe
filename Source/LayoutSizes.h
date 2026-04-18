#pragma once

// Pure-arithmetic sizing and positioning helpers used by the editor's
// layout and by the layout tests. Deliberately kept free of JUCE headers
// so the DSP-only test executable can include this without pulling in
// juce_gui_basics.
//
// The editor's layout has two vertical regions:
//   top: spectrum graph, height = hTotal * graphAreaFrac
//   bottom (the "knob area"): the knobs / labels / button / pill, height
//        hKnobArea = hTotal - graphH
// All `...InKnobArea` helpers are fractions of hKnobArea.

namespace KnobDesign
{
    // Keep this in sync with the editor's spectrum-graph sizing (also
    // exported from KnobDesign.h, which we intentionally can't include here
    // because it drags in juce_gui_basics).
    inline constexpr float graphAreaFrac = 0.36f;

    // Nudge factor the editor applies to the whole knob cluster so it
    // sits a touch lower on the page. Kept here so the position helpers
    // reflect what the editor actually does. Units: pixels at default
    // window height; scale with (hTotal / defaultHeight) in the editor.
    inline constexpr float knobClusterShiftDefault = 20.0f;

    // Default window dimensions — single source of truth for scaling.
    inline constexpr int   defaultWindowWidth  = 650;
    inline constexpr int   defaultWindowHeight = 540;


    // ───────── Column-label text (THRESHOLD / REDUCTION) ─────────

    // Bumped up from 0.024 so the column labels read at the same weight
    // as the START/STOP button used to — they're now the loudest text in
    // the lower half of the window.
    inline constexpr float columnLabelFontSize(float w) { return 0.030f * w; }

    // Label component's height — 1.2x the font leaves room for descenders
    // and a small vertical margin.
    inline constexpr float columnLabelHeight(float w)
    {
        return columnLabelFontSize(w) * 1.2f;
    }

    // Top-Y (within the knob-area sub-window) of the column labels.
    // Dropped from 0.04 to 0.05 — a small "slightly lower" move that's
    // visible without eating into the Reduction knob's "-30" mid-tick
    // label area below.
    inline constexpr float columnLabelTopYInKnobArea() { return 0.05f; }

    // Top-Y of the column labels in editor coordinates.
    inline constexpr float columnLabelTopY(float hTotal)
    {
        const float graphH     = hTotal * graphAreaFrac;
        const float hKnobArea  = hTotal - graphH;
        return graphH + hKnobArea * columnLabelTopYInKnobArea();
    }

    // Worst-case (lowest) editor-coord Y of a column-label's bottom edge.
    inline constexpr float columnLabelBottomY(float w, float hTotal)
    {
        return columnLabelTopY(hTotal) + columnLabelHeight(w);
    }


    // ───────── Reduction knob mid-tick label ("-30") ─────────
    //
    // These mirror drawRotarySlider's geometry so the test can assert the
    // column-label bottom never crosses into the Reduction knob's "-30"
    // label. They use the same scaled knob-cluster shift and the same
    // capped knob diameter (min(sliderW, sliderH) * 0.78 capped to
    // sliderW * 0.60) that the editor uses at the default window.
    inline constexpr float reductionMidTickLabelRectTopY(float w, float hTotal)
    {
        const float knobShift     = knobClusterShiftDefault
                                  * (hTotal / static_cast<float>(defaultWindowHeight))
                                  * 4.5f; // drawRotarySlider uses ~90px at h=540
        const float cyEditor      = hTotal * 0.5f + knobShift;

        // Knob width/diameter — mirrors the editor + drawRotarySlider cap.
        const float sliderW       = w * 0.40f * 0.90f;
        const float diameter      = sliderW * 0.60f;
        const float radius        = diameter * 0.5f;
        const float fontSize      = diameter * 0.18f;
        const float markerFontSz  = fontSize * 0.85f;
        const float tickEndR      = radius * (1.15f + 0.18f);
        const float topLabelR     = tickEndR + markerFontSz * 0.3f;

        // Mid tick label rect top for the Reduction knob (aMid = 0, cos = 1,
        // midYShift = 0 for reduction).
        return cyEditor - topLabelR - markerFontSz;
    }


    // ───────── LEARN button + button label text ─────────

    // START/STOP button font size. Nudged down from 0.030w so the button
    // text doesn't shout over the column labels.
    inline constexpr float learnButtonFontSize(float w) { return 0.026f * w; }

    // Multiplier applied to `learnDiameter * labelFontScale` in the editor
    // to arrive at the "LEARN" / "LEARNING..." text size. Nudged down from
    // 0.65 to 0.55 so the label is visibly smaller than the column labels.
    inline constexpr float learnTextFontScaleFactor() { return 0.55f; }
}
