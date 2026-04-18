#pragma once

// Pure-math constants and helpers for BypassButton's paint routine.
// Kept in a JUCE-free header so tests can link and assert the invariants
// that the icon must stay the same size regardless of toggle state.
namespace BypassButtonMetrics
{
    // Fraction of the button's diameter used for the power glyph's arc
    // radius and stroke width. Same value in engaged and bypassed states
    // by design — the only inter-state delta is the fill / glyph colour,
    // never geometry.
    inline constexpr float kGlyphRadiusFrac = 0.26f;
    inline constexpr float kGlyphStrokeFrac = 0.095f;

    // Half-width of the gap at 12 o'clock in the power ring, in degrees.
    inline constexpr float kBreakHalfDeg = 28.0f;

    // Vertical bar endpoints, expressed as multiples of glyphRadius above
    // the button's centre. Bar starts above the arc's top break and ends
    // a hair below centre, completing the power-symbol silhouette.
    inline constexpr float kBarTopMul    = 1.25f;  // above centre
    inline constexpr float kBarBottomMul = 0.05f;  // above centre

    // Irradiation-illusion compensation.
    //
    // A light-coloured shape on a dark background always *looks* bigger
    // than the same-size dark shape on a light background, even when the
    // rendered pixels are identical. Our engaged state draws a black
    // glyph on an orange fill (dark-on-light); the bypassed state draws
    // an orange glyph on a black fill (light-on-dark). Without a visual
    // correction, the bypassed glyph reads as noticeably larger.
    //
    // kBypassedGlyphScale is applied uniformly to glyph radius AND stroke
    // in the bypassed state only — shrinking the light-on-dark glyph
    // just enough that it LOOKS the same size as the engaged glyph.
    // Tuned empirically: the initial 0.93 wasn't aggressive enough to
    // cancel the illusion at our accent-orange on bgColour contrast, so
    // it was bumped to 0.87. The glyph still reads as the same icon —
    // just with the visual bloat of the light-on-dark state subtracted.
    inline constexpr float kBypassedGlyphScale = 0.87f;

    // ── Derived helpers (no JUCE required — pure float math). ──
    inline constexpr float glyphRadiusForDiameter(float d) noexcept
    {
        return d * kGlyphRadiusFrac;
    }
    inline constexpr float glyphStrokeForDiameter(float d) noexcept
    {
        return d * kGlyphStrokeFrac;
    }

    // Glyph metrics as rendered given the toggle state. Same helpers as
    // above but apply the irradiation-illusion correction in the
    // bypassed state so the perceived icon size matches the engaged
    // icon size. `bypassed = false` returns the exact uncorrected
    // values — parity with the pure helpers.
    inline constexpr float renderedGlyphRadius(float d, bool bypassed) noexcept
    {
        return bypassed ? glyphRadiusForDiameter(d) * kBypassedGlyphScale
                        : glyphRadiusForDiameter(d);
    }
    inline constexpr float renderedGlyphStroke(float d, bool bypassed) noexcept
    {
        return bypassed ? glyphStrokeForDiameter(d) * kBypassedGlyphScale
                        : glyphStrokeForDiameter(d);
    }
}
