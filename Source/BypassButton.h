#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobDesign.h"
#include "BypassButtonMetrics.h"

// Circular bypass button drawn with a power-symbol glyph.
//
// Visual grammar:
//   - Engaged  (toggle == false): orange fill, black power symbol.
//   - Bypassed (toggle == true) : black fill, orange power symbol.
//   - Hover : fill brightens (engaged) / symbol brightens (bypassed).
//   - Press : whole button shrinks a touch so the click "depresses".
//
// Uses JUCE's toggle-state so it pairs with AudioProcessorValueTreeState::
// ButtonAttachment against a bool parameter. No text — the power glyph
// swapping colours is the entire state cue.
class BypassButton : public juce::Button
{
public:
    BypassButton() : juce::Button("Bypass")
    {
        setClickingTogglesState(true);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    // Ring stroke thickness (in pixels) drawn around the disc when the
    // plugin is bypassed. The editor sets this to match the knob stroke so
    // the button reads as part of the same "outlined ring" visual family.
    // The ring is drawn INSIDE the disc's bounds, so it never affects the
    // position or size of the power-symbol glyph.
    void setRingStrokeWidth(float w) { ringStrokeW_ = juce::jmax(0.0f, w); }

    void paintButton(juce::Graphics& g,
                     bool isMouseOver,
                     bool isButtonDown) override
    {
        const bool bypassed = getToggleState();

        auto bounds = getLocalBounds().toFloat();
        // Shrink on press so the button visibly "depresses" under the
        // cursor. Normal state uses the full inner square.
        if (isButtonDown)
            bounds.reduce(bounds.getWidth() * 0.05f, bounds.getHeight() * 0.05f);

        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float radius = diameter * 0.5f;

        // Fill colour logic:
        //   engaged: orange; hover brightens to accentHoverColour
        //   bypassed: bgColour; hover nudges slightly lighter so the button
        //     still feels interactive while the fill stays "off"
        juce::Colour fillColour;
        juce::Colour glyphColour;
        if (bypassed)
        {
            fillColour  = KnobDesign::bgColour;
            glyphColour = isMouseOver
                ? KnobDesign::accentHoverColour
                : KnobDesign::accentColour;
        }
        else
        {
            fillColour = isMouseOver
                ? KnobDesign::accentHoverColour
                : KnobDesign::accentColour;
            glyphColour = KnobDesign::bgColour;
        }

        // Filled disc. No extra ring on the bypassed state — the only
        // thing that changes between engaged and bypassed is the fill /
        // glyph colour swap, so the power icon is visually identical in
        // size, stroke, and position in both states.
        g.setColour(fillColour);
        g.fillEllipse(cx - radius, cy - radius, diameter, diameter);

        // Orange ring drawn AROUND the disc when the plugin is bypassed.
        // Stroked INSIDE the disc bounds so it never eats into the area
        // used by the power glyph — the glyph's position and size are
        // identical in both states. See BypassButtonMetrics for the
        // invariant this guarantees.
        if (bypassed && ringStrokeW_ > 0.0f)
        {
            // Half the knob stroke so the ring reads as a lighter
            // outline around the bypass disc — still of the same visual
            // family, but not as heavy as the knob's own rim.
            const float sw = juce::jmin(ringStrokeW_ * 0.5f, diameter * 0.5f);
            // Brighten on hover, matching the behaviour of the fill in
            // the engaged state.
            g.setColour(isMouseOver ? KnobDesign::accentHoverColour
                                    : KnobDesign::accentColour);
            g.drawEllipse(cx - radius + sw * 0.5f,
                          cy - radius + sw * 0.5f,
                          diameter - sw,
                          diameter - sw,
                          sw);
        }

        // Power glyph — open circle with a break at 12 o'clock, plus a
        // short vertical line dropping through the break into the centre.
        // Sizing goes through BypassButtonMetrics so the glyph geometry
        // is a pure function of the button's diameter — no toggle-state
        // branch here, no ring involvement. This is the invariant the
        // TestBypassButton suite locks down.
        const float glyphRadius = BypassButtonMetrics::glyphRadiusForDiameter(diameter);
        const float glyphStroke = BypassButtonMetrics::glyphStrokeForDiameter(diameter);

        const float startAngle = juce::degreesToRadians(BypassButtonMetrics::kBreakHalfDeg);
        const float endAngle   = juce::degreesToRadians(360.0f - BypassButtonMetrics::kBreakHalfDeg);

        juce::Path arc;
        arc.addCentredArc(cx, cy, glyphRadius, glyphRadius,
                          0.0f,
                          startAngle, endAngle,
                          true);

        g.setColour(glyphColour);
        g.strokePath(arc, juce::PathStrokeType(glyphStroke,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        // Vertical line — starts slightly above the arc's top break and
        // ends a hair below the arc's centre, forming the classic power-
        // symbol "bar through the ring".
        const float lineTopY    = cy - glyphRadius * BypassButtonMetrics::kBarTopMul;
        const float lineBottomY = cy - glyphRadius * BypassButtonMetrics::kBarBottomMul;
        juce::Path bar;
        bar.startNewSubPath(cx, lineTopY);
        bar.lineTo         (cx, lineBottomY);
        g.strokePath(bar, juce::PathStrokeType(glyphStroke,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

private:
    float ringStrokeW_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BypassButton)
};
