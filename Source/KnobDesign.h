#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "LayoutSizes.h"

namespace KnobDesign
{
    // ── Colors (matching conji.us) ──
    inline const juce::Colour bgColour         { 0xff111111 };  // #111
    inline const juce::Colour accentColour     { 0xffd48300 };  // #d48300
    inline const juce::Colour accentHoverColour{ 0xffffb84d };  // much lighter orange for hover/press

    // ── Knob geometry (proportional to diameter) ──
    inline constexpr float knobStrokeFrac    = 0.033f;
    inline constexpr float indicatorWidthFrac= 0.040f;
    inline constexpr float tickStrokeFrac    = 0.033f;

    inline constexpr float indicatorStart    = 0.33f;
    inline constexpr float indicatorEnd      = 0.75f;

    inline constexpr float tickGap           = 1.15f;
    inline constexpr float tickLength        = 0.18f;

    // ── Rotation range ──
    inline constexpr float rotationStartAngle = -135.0f;
    inline constexpr float rotationEndAngle   =  135.0f;

    // ── Label style ──
    inline constexpr float labelFontScale    = 0.18f;
    inline constexpr float gainLabelScale    = 0.06f;
    inline constexpr float dbTextScale       = 0.06f;
    inline constexpr float latencyTextScale  = 0.017f;

    // ── Window ──
    // Taller default than before so the spectrum graph fits above the knobs.
    inline constexpr int   defaultWidth      = 650;
    inline constexpr int   defaultHeight     = 540;
    inline constexpr int   minWidth          = 400;
    inline constexpr int   minHeight         = 420;
    inline constexpr int   maxWidth          = 1000;
    inline constexpr int   maxHeight         = 900;

    // Fraction of the window height occupied by the spectrum graph at the top.
    // graphAreaFrac defined in LayoutSizes.h (same namespace).

    // ── Angle helpers ──
    inline float normToAngleRad(float norm01)
    {
        float degrees = rotationStartAngle + norm01 * (rotationEndAngle - rotationStartAngle);
        return juce::degreesToRadians(degrees);
    }

    // Non-deprecated replacement for juce::Font::getStringWidthFloat (which
    // was marked deprecated in JUCE 8 in favour of GlyphArrangement).
    inline float stringWidth(const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        return ga.getBoundingBox(0, -1, true).getWidth();
    }
}

// ── Knob type: determines tick labels and pill format ──
enum class KnobType
{
    Threshold,   // 0.5× – 5.0×, tick labels: "0.5×", "1.5×", "5.0×"
    Reduction    // -60 dB – 0 dB, tick labels: "-60", "-30", "0"
};

// ── Custom LookAndFeel ──
class ConjusKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ConjusKnobLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, KnobDesign::accentColour);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textColourId, KnobDesign::accentColour);
    }

    void loadFonts(const void* boldData, int boldSize, const void* regularData, int regularSize)
    {
        boldTypeface = juce::Typeface::createSystemTypefaceFor(boldData, static_cast<size_t>(boldSize));
        regularTypeface = juce::Typeface::createSystemTypefaceFor(regularData, static_cast<size_t>(regularSize));
    }

    juce::Font getBoldFont(float height) const
    {
        if (boldTypeface != nullptr)
            return juce::Font(juce::FontOptions(boldTypeface).withHeight(height));
        return juce::Font(juce::FontOptions().withHeight(height).withStyle("Bold"));
    }

    juce::Font getRegularFont(float height) const
    {
        if (regularTypeface != nullptr)
            return juce::Font(juce::FontOptions(regularTypeface).withHeight(height));
        return juce::Font(juce::FontOptions().withHeight(height));
    }

    // Orange corner resizer — three diagonal lines in the accent colour.
    // Default state uses a brighter tint so the affordance is clearly visible
    // at rest (previously very thin 1.5px strokes in the dim accent colour
    // made it look invisible until the user happened to grab it).
    void drawCornerResizer(juce::Graphics& g, int w, int h,
                           bool isMouseOver, bool isMouseDragging) override
    {
        const auto colour = (isMouseOver || isMouseDragging)
                            ? KnobDesign::accentHoverColour
                            : KnobDesign::accentColour.brighter(0.2f);
        g.setColour(colour);

        const float minDim = juce::jmin(static_cast<float>(w), static_cast<float>(h));
        const float inset = minDim * 0.20f;
        const float right = static_cast<float>(w);
        const float bottom = static_cast<float>(h);
        const float strokeW = juce::jmax(2.5f, minDim * 0.11f);

        for (int i = 1; i <= 3; ++i)
        {
            const float t = static_cast<float>(i) * (bottom - inset) / 4.0f;
            juce::Line<float> line{ right - 2.0f, bottom - t - 2.0f,
                                    right - t - 2.0f, bottom - 2.0f };
            g.drawLine(line, strokeW);
        }
    }

    // Store knob type per slider via component properties
    static void setKnobType(juce::Slider& slider, KnobType type)
    {
        slider.getProperties().set("knobType", static_cast<int>(type));
    }

    static KnobType getKnobType(juce::Slider& slider)
    {
        return static_cast<KnobType>(static_cast<int>(slider.getProperties().getWithDefault("knobType", 0)));
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float /*rotaryStartAngle*/, float /*rotaryEndAngle*/,
                          juce::Slider& slider) override
    {
        using namespace KnobDesign;

        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

        // Use the slider's allocated area to determine knob size. The slider
        // bounds now span most of the window height (to contain the knob at
        // windowH/2), so cap diameter to a fraction of slider WIDTH to keep
        // the visual size consistent with the previous "tight" layout.
        float sliderW = bounds.getWidth();
        float sliderH = bounds.getHeight();
        float diameter = juce::jmin(juce::jmin(sliderW, sliderH) * 0.78f,
                                    sliderW * 0.60f);
        float radius = diameter * 0.5f;

        // Knob-graphics offset (ring + ticks + tick labels only — the value
        // pill is drawn in drawLabel() off the slider text box bounds, so
        // it moves via the slider's own setBounds() in the editor's
        // resized(), not via this offset). Raised slightly so the knob
        // cluster sits a touch lower on the page.
        float parentH = 0.0f;
        if (auto* editor = slider.getParentComponent())
            parentH = static_cast<float>(editor->getHeight());
        const float knobShiftDown = 90.0f * (parentH > 0.0f
                                             ? parentH / static_cast<float>(KnobDesign::defaultHeight)
                                             : 1.0f);
        float cx = bounds.getCentreX();
        // Centre the knob at the vertical midpoint of the editor window so the
        // gap to the orange border is identical above and below, THEN apply
        // the knob-graphics-only down-shift so the ring and tick labels move
        // without dragging the value pill (the pill is drawn by drawLabel()
        // against the slider's text-box bounds).
        auto* parent = slider.getParentComponent();
        const float windowH = parent ? static_cast<float>(parent->getHeight()) : bounds.getHeight();
        const float sliderY = static_cast<float>(slider.getY());
        float cy = windowH * 0.5f - sliderY + knobShiftDown;

        float strokeW = diameter * knobStrokeFrac;
        float indW = diameter * indicatorWidthFrac;
        float tickW = diameter * tickStrokeFrac;

        // Interactive colour: smoothly interpolate toward lighter orange based on hover/drag progress
        float hoverProgress = static_cast<float>(
            slider.getProperties().getWithDefault("hoverProgress", 0.0));
        auto interactiveColour = accentColour.interpolatedWith(accentHoverColour, hoverProgress);

        // ── Draw knob circle ──
        g.setColour(interactiveColour);
        g.drawEllipse(cx - radius + strokeW * 0.5f,
                      cy - radius + strokeW * 0.5f,
                      diameter - strokeW,
                      diameter - strokeW,
                      strokeW);

        // ── Draw indicator line ──
        float angle = normToAngleRad(sliderPosProportional);
        float innerR = radius * indicatorStart;
        float outerR = radius * indicatorEnd;

        juce::Path indicator;
        indicator.startNewSubPath(cx + std::sin(angle) * innerR,
                                 cy - std::cos(angle) * innerR);
        indicator.lineTo(cx + std::sin(angle) * outerR,
                         cy - std::cos(angle) * outerR);
        g.strokePath(indicator,
                     juce::PathStrokeType(indW,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded));

        // ── Draw tick marks at min, default, max ──
        float tickStartR = radius * tickGap;
        float tickEndR = radius * (tickGap + tickLength);

        auto knobType = getKnobType(slider);

        // Tick positions: left (min), middle (default), right (max)
        float defaultNorm = 0.0f;
        if (knobType == KnobType::Threshold)
            defaultNorm = (1.5f - 0.5f) / (5.0f - 0.5f);  // 1.5 is default, maps to ~0.222
        else
            defaultNorm = (-30.0f - (-60.0f)) / (0.0f - (-60.0f));  // -30 default, maps to 0.5

        float tickAngles[3] = {
            juce::degreesToRadians(rotationStartAngle),
            normToAngleRad(defaultNorm),
            juce::degreesToRadians(rotationEndAngle)
        };

        // Ticks always use the base accent colour (no hover highlight)
        g.setColour(accentColour);
        for (int i = 0; i < 3; ++i)
        {
            juce::Path tick;
            tick.startNewSubPath(cx + std::sin(tickAngles[i]) * tickStartR,
                                cy - std::cos(tickAngles[i]) * tickStartR);
            tick.lineTo(cx + std::sin(tickAngles[i]) * tickEndR,
                        cy - std::cos(tickAngles[i]) * tickEndR);
            g.strokePath(tick,
                         juce::PathStrokeType(tickW,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
        }

        // Ticks and tick labels always use the base accent colour (no hover highlight)
        // Note: the tick *marks* were drawn in the block above and still use interactiveColour —
        // reset to accentColour so the numeric tick labels don't brighten.
        // ── Draw tick labels ──
        float fontSize = diameter * labelFontScale;
        float markerFontSize = fontSize * 0.85f;
        g.setColour(accentColour);
        g.setFont(getBoldFont(markerFontSize));

        float labelR = tickEndR + markerFontSize * 0.8f;
        float labelYOffset = fontSize * 0.05f;

        juce::String leftLabel, midLabel, rightLabel;
        if (knobType == KnobType::Threshold)
        {
            leftLabel = "0.5";
            midLabel = "1.5";
            rightLabel = "5.0";
        }
        else
        {
            leftLabel = juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + "60";
            midLabel = juce::String(juce::CharPointer_UTF8("\xe2\x88\x92")) + "30";
            rightLabel = "0";
        }

        // Left label
        float a0 = juce::degreesToRadians(rotationStartAngle);
        float lx0 = cx + std::sin(a0) * labelR;
        float ly0 = cy - std::cos(a0) * labelR + labelYOffset;
        g.drawText(leftLabel,
                   juce::Rectangle<float>(lx0 - fontSize * 2.5f, ly0 - markerFontSize * 0.5f,
                                          fontSize * 5.0f, markerFontSize * 1.2f),
                   juce::Justification::centred, false);

        // Right label
        float a1 = juce::degreesToRadians(rotationEndAngle);
        float lx1 = cx + std::sin(a1) * labelR;
        float ly1 = cy - std::cos(a1) * labelR + labelYOffset;
        g.drawText(rightLabel,
                   juce::Rectangle<float>(lx1 - fontSize * 2.5f, ly1 - markerFontSize * 0.5f,
                                          fontSize * 5.0f, markerFontSize * 1.2f),
                   juce::Justification::centred, false);

        // Middle label (above tick)
        float aMid = normToAngleRad(defaultNorm);
        float topLabelR = tickEndR + markerFontSize * 0.3f;
        float midLabelW = KnobDesign::stringWidth(getBoldFont(markerFontSize), midLabel);
        float midXShift = (knobType == KnobType::Threshold) ? -midLabelW * 0.5f : 0.0f;
        float midYShift = (knobType == KnobType::Threshold) ? markerFontSize * 0.3f : 0.0f;
        float lxM = cx + std::sin(aMid) * topLabelR + midXShift;
        float lyM = cy - std::cos(aMid) * topLabelR - markerFontSize * 0.5f + midYShift;
        g.drawText(midLabel,
                   juce::Rectangle<float>(lxM - fontSize * 2.5f, lyM - markerFontSize * 0.5f,
                                          fontSize * 5.0f, markerFontSize * 1.2f),
                   juce::Justification::centred, false);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        bool isSliderTextBox = (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr);

        if (isSliderTextBox)
        {
            auto text = label.getText();
            auto* slider = dynamic_cast<juce::Slider*>(label.getParentComponent());
            auto knobType = slider ? getKnobType(*slider) : KnobType::Threshold;

            // Determine suffix
            juce::String suffix = (knobType == KnobType::Threshold) ? juce::String(juce::CharPointer_UTF8("\xc3\x97")) : " dB";

            // Use proportional font sizes
            auto* editor = slider ? slider->getParentComponent() : nullptr;
            float windowH = editor ? static_cast<float>(editor->getHeight()) : 450.0f;
            float pillFontSize = windowH * 0.042f;
            auto pillFont = getBoldFont(pillFontSize);
            float suffixFontSize = (knobType == KnobType::Threshold) ? pillFontSize * 1.3f : pillFontSize;
            auto suffixFont = (knobType == KnobType::Threshold) ? getRegularFont(suffixFontSize) : getBoldFont(suffixFontSize);

            float suffixW = KnobDesign::stringWidth(suffixFont, suffix);

            // Parse value from text
            juce::String valueStr = text;
            // Strip suffix for display
            if (knobType == KnobType::Reduction)
            {
                valueStr = text.replace(" dB", "").trim();
            }

            bool isNegative = valueStr.getDoubleValue() < 0.0;
            juce::String digits = valueStr;
            if (digits.startsWith("-"))
                digits = digits.substring(1);
            if (digits.startsWith("+"))
                digits = digits.substring(1);

            // Minus sign
            auto minusStr = juce::String(juce::CharPointer_UTF8("\xe2\x88\x92"));
            float minusW = KnobDesign::stringWidth(pillFont, minusStr);
            float minusGap = pillFontSize * 0.15f;

            float digitsW = KnobDesign::stringWidth(pillFont, digits);
            float numW = KnobDesign::stringWidth(pillFont, "99.9");
            float valueZoneW = juce::jmax(digitsW, numW);

            float pillH = pillFontSize * 1.5f;
            float padLeft = pillH * 0.45f;
            float padRight = pillH * 0.3f;

            // Both pills use the same width (matched to the wider reduction layout)
            // Compute reduction-style width: [pad | minus | gap | digits | dB | pad]
            auto reductionSuffix = juce::String(" dB");
            float reductionSuffixW = KnobDesign::stringWidth(getBoldFont(pillFontSize), reductionSuffix);
            float pillW = padLeft + minusW + minusGap + valueZoneW + reductionSuffixW + padRight;

            float labelW = static_cast<float>(label.getWidth());
            float pillX = (labelW - pillW) * 0.5f;
            // pillUpShift reduced so the pill sits ~½ pillH lower on screen
            // (tightens the gap to the bottom orange border).
            float pillUpShift = pillFontSize * 1.25f;
            float pillY = (static_cast<float>(label.getHeight()) - pillH) * 0.5f - pillUpShift;

            auto pillBounds = juce::Rectangle<float>(pillX, pillY, pillW, pillH);

            // Interactive pill colour: smoothly interpolate based on parent slider's hover progress
            float pillHoverProgress = 0.0f;
            if (auto* parentSlider = label.findParentComponentOfClass<juce::Slider>())
                pillHoverProgress = static_cast<float>(
                    parentSlider->getProperties().getWithDefault("hoverProgress", 0.0));
            auto pillFillColour = KnobDesign::accentColour
                .interpolatedWith(KnobDesign::accentHoverColour, pillHoverProgress);

            // Draw orange pill
            g.setColour(pillFillColour);
            g.fillRoundedRectangle(pillBounds, pillH * 0.5f);

            g.setColour(KnobDesign::bgColour);
            float centreY = pillBounds.getCentreY();

            if (knobType == KnobType::Threshold)
            {
                // × on left
                float suffixX = pillBounds.getX() + padRight;
                g.setFont(suffixFont);
                g.drawText(suffix,
                           juce::Rectangle<float>(suffixX, centreY - suffixFontSize * 0.5f,
                                                  suffixW, suffixFontSize),
                           juce::Justification::centred, false);

                // Digits centered horizontally in the pill
                g.setFont(pillFont);
                g.drawText(digits,
                           juce::Rectangle<float>(pillBounds.getX(), centreY - pillFontSize * 0.5f,
                                                  pillBounds.getWidth(), pillFontSize),
                           juce::Justification::centred, false);
            }
            else
            {
                // Suffix (dB) on right
                float suffixX = pillBounds.getRight() - padRight - suffixW;
                g.setFont(suffixFont);
                g.drawText(suffix,
                           juce::Rectangle<float>(suffixX, centreY - suffixFontSize * 0.5f,
                                                  suffixW, suffixFontSize),
                           juce::Justification::centred, false);

                // Minus sign on left
                float minusX = pillBounds.getX() + padLeft;
                float valueLeft = minusX + minusW + minusGap;

                if (isNegative)
                {
                    g.setFont(pillFont);
                    g.drawText(minusStr,
                               juce::Rectangle<float>(minusX, centreY - pillFontSize * 0.5f,
                                                      minusW, pillFontSize),
                               juce::Justification::centred, false);
                }

                // Value digits
                g.setFont(pillFont);
                g.drawText(digits,
                           juce::Rectangle<float>(valueLeft, centreY - pillFontSize * 0.5f,
                                                  suffixX - valueLeft, pillFontSize),
                           juce::Justification::centredLeft, false);
            }
        }
        else
        {
            LookAndFeel_V4::drawLabel(g, label);
        }
    }

private:
    juce::Typeface::Ptr boldTypeface;
    juce::Typeface::Ptr regularTypeface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConjusKnobLookAndFeel)
};
