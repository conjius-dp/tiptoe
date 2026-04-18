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

    // ── Derived helpers (no JUCE required — pure float math). ──
    inline constexpr float glyphRadiusForDiameter(float d) noexcept
    {
        return d * kGlyphRadiusFrac;
    }
    inline constexpr float glyphStrokeForDiameter(float d) noexcept
    {
        return d * kGlyphStrokeFrac;
    }
}
