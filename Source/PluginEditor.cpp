#include "PluginEditor.h"

TiptoeAudioProcessorEditor::TiptoeAudioProcessorEditor(TiptoeAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    conjusLAF.loadFonts(BinaryData::InconsolataBold_ttf,
                        BinaryData::InconsolataBold_ttfSize,
                        BinaryData::InconsolataRegular_ttf,
                        BinaryData::InconsolataRegular_ttfSize);
    setLookAndFeel(&conjusLAF);

    // ── Threshold knob (0.5× – 5.0×) ──
    thresholdSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 30);
    ConjusKnobLookAndFeel::setKnobType(thresholdSlider, KnobType::Threshold);
    thresholdSlider.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(thresholdSlider);

    // ── Reduction knob (-60 dB – 0 dB) ──
    reductionSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 30);
    ConjusKnobLookAndFeel::setKnobType(reductionSlider, KnobType::Reduction);
    reductionSlider.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(reductionSlider);

    // ── Labels ──
    thresholdLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(thresholdLabel);

    reductionLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reductionLabel);

    // ── Attachments ──
    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "threshold", thresholdSlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "reduction", reductionSlider);

    // Set text functions AFTER attachment
    thresholdSlider.textFromValueFunction = [](double value) -> juce::String {
        return juce::String(value, 1);
    };
    thresholdSlider.valueFromTextFunction = [](const juce::String& text) -> double {
        return text.getDoubleValue();
    };
    thresholdSlider.updateText();

    reductionSlider.textFromValueFunction = [](double value) -> juce::String {
        return juce::String(value, 1) + " dB";
    };
    reductionSlider.valueFromTextFunction = [](const juce::String& text) -> double {
        return text.getDoubleValue();
    };
    reductionSlider.updateText();

    // ── Animated snap to default on double-click ──
    thresholdSlider.onDoubleClick = [this]() {
        startSnapAnimation(thresholdSlider, thresholdAnim);
    };
    reductionSlider.onDoubleClick = [this]() {
        startSnapAnimation(reductionSlider, reductionAnim);
    };

    // ── Learn button (orange pill style) ──
    // Visual state is animated via the "stateTarget" / "stateProgress" properties
    // (0 = Start/orange fill, 1 = Stop/dark fill with border). The editor's timer lerps
    // stateProgress toward stateTarget each frame.
    learnButton.getProperties().set("stateTarget", 0.0);
    learnButton.getProperties().set("stateProgress", 0.0);
    learnButton.onClick = [this]()
    {
        if (processorRef.isLearning())
        {
            processorRef.stopLearning();
            learnButton.setButtonText("Start");
            learnButton.getProperties().set("stateTarget", 0.0);
        }
        else
        {
            processorRef.startLearning();
            learnButton.setButtonText("Stop");
            learnButton.getProperties().set("stateTarget", 1.0);
        }
    };
    learnButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(learnButton);

    // ── Latency label ──
    latencyLabel.setJustificationType(juce::Justification::centredLeft);
    latencyLabel.setColour(juce::Label::textColourId, KnobDesign::accentColour.darker(0.3f));
    latencyLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(latencyLabel);

    latencyHitArea.onClick = [this]() { latencyHidden = !latencyHidden; };
    latencyHitArea.onHover = [this](bool over) { latencyHoverTarget = over; };
    addAndMakeVisible(latencyHitArea);
    latencyHitArea.toFront(false);

    // ── Logo ──
    logoImage = juce::ImageCache::getFromMemory(
        BinaryData::conjiusavatartransparentbg_png,
        BinaryData::conjiusavatartransparentbg_pngSize);

    titleLogoImage = juce::ImageCache::getFromMemory(
        BinaryData::tiptoelogoorange_png,
        BinaryData::tiptoelogoorange_pngSize);

    // ── Window ──
    int savedW = processorRef.editorWidth.load();
    int savedH = processorRef.editorHeight.load();
    setSize(savedW, savedH);
    setResizable(true, true);
    setResizeLimits(KnobDesign::minWidth, KnobDesign::minHeight,
                    KnobDesign::maxWidth, KnobDesign::maxHeight);
    getConstrainer()->setFixedAspectRatio(
        static_cast<double>(KnobDesign::defaultWidth) / KnobDesign::defaultHeight);

    // Receive mouse events from self and child components (for conjius logo hover)
    addMouseListener(this, true);

    startTimerHz(60);
}

TiptoeAudioProcessorEditor::~TiptoeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void TiptoeAudioProcessorEditor::mouseMove(const juce::MouseEvent& e)
{
    auto pos = getLocalPoint(e.eventComponent, e.getPosition());
    bool inside = logoBounds.contains(pos);
    if (inside != logoHoverTarget)
        logoHoverTarget = inside;
}

void TiptoeAudioProcessorEditor::mouseExit(const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
        logoHoverTarget = false;
}

void TiptoeAudioProcessorEditor::timerCallback()
{
    static int latencyTick = 0;
    // Update latency text only every 12th frame (~5 Hz) so the number is comfortably readable
    if (++latencyTick >= 12)
    {
        latencyTick = 0;
        float ms = processorRef.getLastProcessingTimeMs();
        latencyLabel.setText("Latency: " + juce::String(ms, 3) + "ms",
                             juce::dontSendNotification);
    }

    // Animate conjius logo hover state
    float target = logoHoverTarget ? 1.0f : 0.0f;
    if (std::abs(target - logoHoverProgress) > 0.002f)
    {
        logoHoverProgress += (target - logoHoverProgress) * 0.18f;
        repaint(logoBounds.expanded(static_cast<int>(logoBounds.getWidth() * 0.2f)));
    }

    // Animate hover colour interpolation for knobs and Learn button
    auto animateHover = [](juce::Component& c, bool target) {
        float current = static_cast<float>(c.getProperties().getWithDefault("hoverProgress", 0.0));
        float dest = target ? 1.0f : 0.0f;
        if (std::abs(dest - current) > 0.002f)
        {
            current += (dest - current) * 0.22f;
            c.getProperties().set("hoverProgress", current);
            c.repaint();
        }
    };
    animateHover(thresholdSlider, thresholdSlider.isMouseOverOrDragging(true));
    animateHover(reductionSlider, reductionSlider.isMouseOverOrDragging(true));
    animateHover(learnButton,     learnButton.isOver() || learnButton.isDown());

    // Update snap-to-default animations
    updateSnapAnimation(thresholdSlider, thresholdAnim);
    updateSnapAnimation(reductionSlider, reductionAnim);

    // Update learn button state (in case processor state changed externally)
    if (processorRef.isLearning() && learnButton.getButtonText() != "Stop")
    {
        learnButton.setButtonText("Stop");
        learnButton.getProperties().set("stateTarget", 1.0);
    }
    else if (!processorRef.isLearning() && learnButton.getButtonText() != "Start")
    {
        learnButton.setButtonText("Start");
        learnButton.getProperties().set("stateTarget", 0.0);
    }

    // Animate Learn button visual state transition (Start <-> Stop)
    {
        auto& props = learnButton.getProperties();
        float target = static_cast<float>(props.getWithDefault("stateTarget", 0.0));
        float current = static_cast<float>(props.getWithDefault("stateProgress", 0.0));
        if (std::abs(target - current) > 0.002f)
        {
            current += (target - current) * 0.20f;
            props.set("stateProgress", current);
            learnButton.repaint();
        }
    }

    // Animate "Learn" <-> "Learning..." text crossfade and the three-dot loop
    {
        float textTarget = processorRef.isLearning() ? 1.0f : 0.0f;
        bool needRepaint = false;

        if (std::abs(textTarget - learningTextProgress) > 0.002f)
        {
            learningTextProgress += (textTarget - learningTextProgress) * 0.20f;
            needRepaint = true;
        }

        // Run the dot loop whenever "Learning..." is visible to any degree
        if (processorRef.isLearning() || learningTextProgress > 0.01f)
        {
            dotAnimPhase += 1.0f / 90.0f;  // ~1.5s per full cycle at 60 Hz
            if (dotAnimPhase >= 1.0f) dotAnimPhase -= 1.0f;
            needRepaint = true;
        }

        if (needRepaint && !learnTextBounds.isEmpty())
            repaint(learnTextBounds);
    }

    // Animate latency label: grows 3x on hover in both modes; slides out when hidden
    {
        float hoverDest = latencyHoverTarget ? 1.0f : 0.0f;
        if (std::abs(hoverDest - latencyHoverProgress) > 0.002f)
            latencyHoverProgress += (hoverDest - latencyHoverProgress) * 0.22f;

        float hideDest = latencyHidden ? (latencyHoverTarget ? 0.0f : 1.0f) : 0.0f;
        if (std::abs(hideDest - latencyHideProgress) > 0.002f)
            latencyHideProgress += (hideDest - latencyHideProgress) * 0.18f;

        if (!latencyBaseBounds.isEmpty())
        {
            float scale = 1.0f + 0.6f * latencyHoverProgress; // 1.0 → 1.6x
            latencyLabel.setFont(conjusLAF.getRegularFont(latencyBaseFontSize * scale));

            float slidePx = static_cast<float>(latencyBaseBounds.getHeight()) * 2.0f * latencyHideProgress;
            int scaledH = static_cast<int>(latencyBaseBounds.getHeight() * scale);
            int extra = scaledH - latencyBaseBounds.getHeight();
            auto bounds = latencyBaseBounds.withY(latencyBaseBounds.getY() - extra)
                                           .withHeight(scaledH)
                                           .translated(0, static_cast<int>(slidePx));
            latencyLabel.setBounds(bounds);
        }
    }
}

void TiptoeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(KnobDesign::bgColour);

    // Draw conjius logo in bottom-left corner — darker by default, brightens and grows on hover
    if (logoImage.isValid())
    {
        float scale = static_cast<float>(getWidth()) / static_cast<float>(KnobDesign::defaultWidth);
        int baseSize = static_cast<int>(37.5f * scale);
        int padLeft = baseSize / 3;
        int baseX = padLeft;
        int baseY = getHeight() - baseSize;
        logoBounds = { baseX, baseY, baseSize, baseSize };

        float hoverScale = 1.0f + 0.2f * logoHoverProgress; // 1.0 -> 1.2
        int drawSize = static_cast<int>(baseSize * hoverScale);
        // Grow around the centre of the base rect so the logo feels like it "pops"
        int drawX = baseX + (baseSize - drawSize) / 2;
        int drawY = baseY + (baseSize - drawSize) / 2;

        // Dim by default (0.35 opacity), brighten on hover (to 1.0)
        float brightness = 0.35f + 0.65f * logoHoverProgress;
        g.setOpacity(brightness);
        g.drawImage(logoImage,
                    drawX, drawY, drawSize, drawSize,
                    0, 0, logoImage.getWidth(), logoImage.getHeight());
        g.setOpacity(1.0f);
    }

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());

    // Draw title logo at top-centre (small)
    if (titleLogoImage.isValid())
    {
        float titleH = h * 0.146f; // 1.5x the previous size
        float aspect = static_cast<float>(titleLogoImage.getWidth())
                     / static_cast<float>(titleLogoImage.getHeight());
        float titleW = titleH * aspect;
        float titleX = (w - titleW) * 0.5f;
        float titleY = h * 0.055f; // slightly lower than before
        g.drawImage(titleLogoImage,
                    juce::Rectangle<float>(titleX, titleY, titleW, titleH),
                    juce::RectanglePlacement::centred);

        // Subtitle: small "Spectral denoiser" tagline diagonally below/right of the logo
        float subFontSize = h * 0.028f;
        g.setColour(KnobDesign::accentHoverColour);
        g.setFont(conjusLAF.getBoldFont(subFontSize));
        float subX = titleX + titleW * 0.55f;
        float subY = titleY + titleH * 0.70f;
        g.drawText("Spectral denoiser",
                   juce::Rectangle<float>(subX, subY, w * 0.4f, subFontSize * 1.4f),
                   juce::Justification::topLeft, false);
    }

    // Draw "Learn" / "Learning..." label above the button — size matches knob tick labels
    float textBoxH_est = w * KnobDesign::dbTextScale * 2.6f;
    float sliderBoundsW_est = w * 0.40f * 0.90f;
    auto tsBounds = thresholdSlider.getBounds();
    float knobAreaH_est = tsBounds.isEmpty()
        ? (h * 0.96f - (h * 0.05f + 50.0f * (h / static_cast<float>(KnobDesign::defaultHeight))
                        + h * 0.14f + h * 0.09f)) - textBoxH_est
        : static_cast<float>(tsBounds.getHeight()) - textBoxH_est;
    float learnDiameter = juce::jmin(sliderBoundsW_est, knobAreaH_est) * 0.78f;
    float learnFontSize = learnDiameter * KnobDesign::labelFontScale * 0.85f; // == markerFontSize

    auto labelFontLearn = conjusLAF.getBoldFont(learnFontSize);
    g.setFont(labelFontLearn);

    // Centre column between the two knobs — text sits just above the button
    float centreX = w * 0.5f;
    float btnH = h * 0.07f;
    float btnY = h * 0.68f - btnH * 0.5f;
    float labelY = btnY - learnFontSize * 1.85f;

    // Crossfade alphas between "Learn" and "Learning" (without the dots)
    float alphaLearn    = juce::jmax(0.0f, 1.0f - 2.0f * learningTextProgress);
    float alphaLearning = juce::jmax(0.0f, 2.0f * learningTextProgress - 1.0f);

    // "Learn" (static, centred)
    if (alphaLearn > 0.001f)
    {
        g.setColour(KnobDesign::accentColour.withMultipliedAlpha(alphaLearn));
        g.drawText("Learn",
                   juce::Rectangle<float>(centreX - w * 0.15f, labelY, w * 0.3f, learnFontSize * 1.2f),
                   juce::Justification::centred, false);
    }

    // "Learning" + animated dots
    if (alphaLearning > 0.001f)
    {
        float learningW = labelFontLearn.getStringWidthFloat("Learning");
        float dotW      = labelFontLearn.getStringWidthFloat(".");
        float dotSpacing = dotW * 0.55f; // tighter than the glyph advance
        float fullW     = learningW + 3.0f * dotSpacing;

        float baseX = centreX - fullW * 0.5f;
        auto colour = KnobDesign::accentColour.withMultipliedAlpha(alphaLearning);

        g.setColour(colour);
        g.drawText("Learning",
                   juce::Rectangle<float>(baseX, labelY, learningW, learnFontSize * 1.2f),
                   juce::Justification::centredLeft, false);

        // 3 dots, appear left→right, disappear right→left, loop.
        // Cycle phases for each dot (in terms of dotAnimPhase 0..1):
        //   fadeIn [start, end], visible until fadeOut start, fadeOut [start, end]
        struct DotPhase { float inStart, inEnd, outStart, outEnd; };
        const DotPhase dots[3] = {
            {0.00f, 0.15f, 0.80f, 0.90f}, // leftmost dot
            {0.15f, 0.30f, 0.70f, 0.80f},
            {0.30f, 0.45f, 0.60f, 0.70f}  // rightmost dot
        };

        float slideDistance = learnFontSize * 0.4f; // how far each dot emerges from below
        float dotBaseY = labelY;

        for (int i = 0; i < 3; ++i)
        {
            const auto& d = dots[i];
            float a = 0.0f;
            float slide = 0.0f; // 0 = at baseline, 1 = below baseline

            if (dotAnimPhase < d.inStart)
            {
                a = 0.0f; slide = 1.0f;
            }
            else if (dotAnimPhase < d.inEnd)
            {
                float t = (dotAnimPhase - d.inStart) / (d.inEnd - d.inStart);
                a = t; slide = 1.0f - t;
            }
            else if (dotAnimPhase < d.outStart)
            {
                a = 1.0f; slide = 0.0f;
            }
            else if (dotAnimPhase < d.outEnd)
            {
                float t = (dotAnimPhase - d.outStart) / (d.outEnd - d.outStart);
                a = 1.0f - t; slide = t;
            }
            // else: invisible after outEnd

            if (a <= 0.001f)
                continue;

            float dotX = baseX + learningW + static_cast<float>(i) * dotSpacing;
            float dotY = dotBaseY + slide * slideDistance;
            g.setColour(KnobDesign::accentColour.withMultipliedAlpha(a * alphaLearning));
            g.drawText(".",
                       juce::Rectangle<float>(dotX, dotY, dotW, learnFontSize * 1.2f),
                       juce::Justification::centredLeft, false);
        }
    }

    // Cache the area used for the label so the timer can repaint it cheaply
    learnTextBounds = juce::Rectangle<int>(
        static_cast<int>(centreX - w * 0.20f),
        static_cast<int>(labelY - learnFontSize * 0.2f),
        static_cast<int>(w * 0.40f),
        static_cast<int>(learnFontSize * 2.2f));
}

void TiptoeAudioProcessorEditor::resized()
{
    processorRef.editorWidth.store(getWidth());
    processorRef.editorHeight.store(getHeight());

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float margin = w * 0.05f;

    // Title logo takes up top slice; shift knob/button UI down by this amount
    float titleOffset = h * 0.18f;
    // Labels sit higher — just below the title, with smaller offset
    float labelTitleOffset = h * 0.14f;
    // Extra vertical space between parameter labels and the knobs
    float labelKnobGap = h * 0.09f;

    // Two knob columns: left 40%, right 40%, centre 20%
    float knobColW = w * 0.40f;
    float centreColW = w * 0.20f;
    float knobColX0 = margin;
    float knobColX1 = w - margin - knobColW;

    // ── Parameter labels at top ──
    float labelFontSize = w * KnobDesign::gainLabelScale / 2.5f * 1.3f;
    auto labelFont = conjusLAF.getBoldFont(labelFontSize);
    thresholdLabel.setFont(labelFont);
    reductionLabel.setFont(labelFont);

    int labelH = static_cast<int>(labelFontSize * 1.2f);
    int labelY = static_cast<int>(h * 0.05f + 50.0f * (h / static_cast<float>(KnobDesign::defaultHeight)) + labelTitleOffset);
    thresholdLabel.setBounds(static_cast<int>(knobColX0), labelY,
                             static_cast<int>(knobColW), labelH);
    reductionLabel.setBounds(static_cast<int>(knobColX1), labelY,
                             static_cast<int>(knobColW), labelH);

    // ── Knob sliders ──
    float dbFontSize = w * KnobDesign::dbTextScale;
    int sliderTop = labelY + labelH + static_cast<int>(labelKnobGap);
    // Extend slider bounds so growing textBoxH (for tick/pill separation) doesn't shrink the knob
    int sliderBottom = static_cast<int>(h * 0.96f);
    int sliderH = sliderBottom - sliderTop;

    // Tighten slider bounds to match visible knob area
    float sliderBoundsW = knobColW * 0.90f;
    float sliderOffset0 = knobColX0 + (knobColW - sliderBoundsW) * 0.5f;
    float sliderOffset1 = knobColX1 + (knobColW - sliderBoundsW) * 0.5f;

    int textBoxW = static_cast<int>(sliderBoundsW * 0.95f);
    int textBoxH = static_cast<int>(dbFontSize * 2.6f);

    thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);
    thresholdSlider.setMouseDragSensitivity(static_cast<int>(w * 0.5f));
    thresholdSlider.setBounds(static_cast<int>(sliderOffset0), sliderTop,
                              static_cast<int>(sliderBoundsW), sliderH);

    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);
    reductionSlider.setMouseDragSensitivity(static_cast<int>(w * 0.5f));
    reductionSlider.setBounds(static_cast<int>(sliderOffset1), sliderTop,
                              static_cast<int>(sliderBoundsW), sliderH);

    // Update text box fonts and allow pills to paint above label bounds
    for (auto* slider : { &thresholdSlider, &reductionSlider })
    {
        slider->setPaintingIsUnclipped(true);
        if (auto* textBox = slider->getChildComponent(0))
        {
            if (auto* label = dynamic_cast<juce::Label*>(textBox))
            {
                label->setFont(conjusLAF.getRegularFont(dbFontSize));
                label->setMinimumHorizontalScale(1.0f);
                label->setColour(juce::Label::textColourId, KnobDesign::accentColour);
                label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
                label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
                label->setInterceptsMouseClicks(false, false);
                label->setPaintingIsUnclipped(true);
            }
        }
    }

    // ── Learn button (centred pill between knobs, slightly below vertical centre) ──
    float btnW = centreColW * 0.75f;
    float btnH = h * 0.07f;
    float btnX = w * 0.5f - btnW * 0.5f;
    float btnY = h * 0.68f - btnH * 0.5f;

    learnButton.setBounds(static_cast<int>(btnX), static_cast<int>(btnY),
                          static_cast<int>(btnW), static_cast<int>(btnH));

    // Pill with fully circular side edges + larger text
    float btnFontSize = w * KnobDesign::gainLabelScale * 0.5f;
    learnButton.setConnectedEdges(0);

    // Custom LookAndFeel for pill shape button
    struct PillButtonLAF : juce::LookAndFeel_V4
    {
        juce::Font font;
        float knobStrokeW;
        PillButtonLAF(juce::Font f, float ksw) : font(f), knobStrokeW(ksw) {}

        static juce::Rectangle<float> pressBounds(juce::Button& button, bool isButtonDown)
        {
            auto b = button.getLocalBounds().toFloat();
            if (isButtonDown)
                b = b.reduced(b.getWidth() * 0.04f, b.getHeight() * 0.10f);
            return b;
        }

        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& /*backgroundColour*/,
                                  bool /*isMouseOver*/, bool isButtonDown) override
        {
            auto bounds = pressBounds(button, isButtonDown);
            float cornerR = bounds.getHeight() * 0.5f;
            float hoverProgress = static_cast<float>(
                button.getProperties().getWithDefault("hoverProgress", 0.0));
            float stateProgress = static_cast<float>(
                button.getProperties().getWithDefault("stateProgress", 0.0));

            auto interactiveAccent = KnobDesign::accentColour
                .interpolatedWith(KnobDesign::accentHoverColour, hoverProgress);

            // Fill: smoothly morph from orange (Start) to dark bg (Stop)
            auto fill = interactiveAccent.interpolatedWith(KnobDesign::bgColour, stateProgress);
            g.setColour(fill);
            g.fillRoundedRectangle(bounds, cornerR);

            // Border: 0 width on Start, ramps up to 70% of knob stroke on Stop
            float stopBorderW = knobStrokeW * 0.70f;
            float borderW = stopBorderW * stateProgress;
            if (borderW > 0.001f)
            {
                g.setColour(interactiveAccent);
                g.drawRoundedRectangle(bounds.reduced(borderW * 0.5f), cornerR, borderW);
            }
        }
        void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                            bool /*isMouseOver*/, bool isButtonDown) override
        {
            auto bounds = pressBounds(button, isButtonDown);
            float hoverProgress = static_cast<float>(
                button.getProperties().getWithDefault("hoverProgress", 0.0));
            float stateProgress = static_cast<float>(
                button.getProperties().getWithDefault("stateProgress", 0.0));

            auto interactiveAccent = KnobDesign::accentColour
                .interpolatedWith(KnobDesign::accentHoverColour, hoverProgress);
            // Text colour morphs from dark (Start, on orange fill) to orange (Stop, on dark fill)
            auto textColour = KnobDesign::bgColour.interpolatedWith(interactiveAccent, stateProgress);

            // Crossfade between "Start" and "Stop": old text fades out in the first half,
            // new text fades in in the second half. Works in both click directions.
            float alphaStart = juce::jmax(0.0f, 1.0f - 2.0f * stateProgress);
            float alphaStop  = juce::jmax(0.0f, 2.0f * stateProgress - 1.0f);

            g.setFont(font);
            if (alphaStart > 0.001f)
            {
                g.setColour(textColour.withMultipliedAlpha(alphaStart));
                g.drawText("Start", bounds, juce::Justification::centred, false);
            }
            if (alphaStop > 0.001f)
            {
                g.setColour(textColour.withMultipliedAlpha(alphaStop));
                g.drawText("Stop", bounds, juce::Justification::centred, false);
            }
        }
    };
    static PillButtonLAF* pillLAF = nullptr;
    delete pillLAF;
    // Compute knob stroke width in pixels to match actual knob border thickness.
    // Use the same diameter formula as drawRotarySlider: jmin(sliderW, knobAreaH) * 0.78.
    float knobAreaH = static_cast<float>(sliderH) - static_cast<float>(textBoxH);
    float knobDiameter = juce::jmin(sliderBoundsW, knobAreaH) * 0.78f;
    float knobStrokeW = knobDiameter * KnobDesign::knobStrokeFrac;
    pillLAF = new PillButtonLAF(conjusLAF.getBoldFont(btnFontSize), knobStrokeW);
    learnButton.setLookAndFeel(pillLAF);

    // ── Latency label ──
    float latencyFontSize = w * KnobDesign::latencyTextScale;
    latencyLabel.setFont(conjusLAF.getRegularFont(latencyFontSize));
    latencyLabel.setJustificationType(juce::Justification::centredBottom);
    int latencyH = static_cast<int>(latencyFontSize * 2.0f);
    latencyBaseBounds = { 0, getHeight() - latencyH, getWidth(), latencyH };
    latencyBaseFontSize = latencyFontSize;
    // Hit area: narrow — matches the actual text width with a small horizontal pad
    auto latencyFont = conjusLAF.getRegularFont(latencyFontSize);
    int textW = static_cast<int>(latencyFont.getStringWidthFloat("Latency: 0.000ms"));
    int hitPadX = static_cast<int>(latencyFontSize * 0.8f);
    int hitPadY = latencyH;
    int hitW = textW + 2 * hitPadX;
    int hitX = (getWidth() - hitW) / 2;
    latencyHitArea.setBounds(hitX, getHeight() - latencyH - hitPadY, hitW, latencyH + hitPadY);
    latencyHitArea.toFront(false);
    int slideOffset = static_cast<int>(latencyH * 2.0f * latencyHideProgress);
    latencyLabel.setBounds(latencyBaseBounds.translated(0, slideOffset));
}

void TiptoeAudioProcessorEditor::startSnapAnimation(juce::Slider& slider, SliderAnimation& anim)
{
    auto* param = processorRef.getAPVTS().getParameter(
        &slider == &thresholdSlider ? "threshold" : "reduction");
    if (param == nullptr) return;

    anim.currentValue = slider.getValue();
    anim.targetValue = static_cast<double>(param->getDefaultValue())
                       * (slider.getMaximum() - slider.getMinimum()) + slider.getMinimum();
    anim.active = true;
}

void TiptoeAudioProcessorEditor::updateSnapAnimation(juce::Slider& slider, SliderAnimation& anim)
{
    if (!anim.active) return;

    constexpr double smoothing = 0.15; // lerp factor per frame at 30Hz
    anim.currentValue += (anim.targetValue - anim.currentValue) * smoothing;

    // Snap when close enough
    if (std::abs(anim.targetValue - anim.currentValue) < 0.01)
    {
        anim.currentValue = anim.targetValue;
        anim.active = false;
    }

    slider.setValue(anim.currentValue, juce::sendNotificationAsync);
}
