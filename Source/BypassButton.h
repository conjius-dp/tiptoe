#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "KnobDesign.h"

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

        // Filled disc.
        g.setColour(fillColour);
        g.fillEllipse(cx - radius, cy - radius, diameter, diameter);

        // Orange ring around the circle in bypassed state so the button
        // still reads as a pressable affordance against the plugin bg.
        if (bypassed)
        {
            const float ringW = diameter * 0.06f;
            g.setColour(glyphColour);
            g.drawEllipse(cx - radius + ringW * 0.5f,
                          cy - radius + ringW * 0.5f,
                          diameter - ringW,
                          diameter - ringW,
                          ringW);
        }

        // Power glyph — open circle with a break at 12 o'clock, plus a
        // short vertical line dropping through the break into the centre.
        // All sized as fractions of the button diameter.
        const float glyphRadius = diameter * 0.26f;
        const float glyphStroke = diameter * 0.095f;
        const float breakHalfDeg = 28.0f; // half-width of the gap at top

        const float startAngle = juce::degreesToRadians(breakHalfDeg);
        const float endAngle   = juce::degreesToRadians(360.0f - breakHalfDeg);

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
        const float lineTopY    = cy - glyphRadius * 1.25f;
        const float lineBottomY = cy - glyphRadius * 0.05f;
        juce::Path bar;
        bar.startNewSubPath(cx, lineTopY);
        bar.lineTo         (cx, lineBottomY);
        g.strokePath(bar, juce::PathStrokeType(glyphStroke,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BypassButton)
};
