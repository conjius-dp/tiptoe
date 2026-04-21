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
    sensitivitySlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    sensitivitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 30);
    ConjusKnobLookAndFeel::setKnobType(sensitivitySlider, KnobType::Sensitivity);
    sensitivitySlider.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(sensitivitySlider);

    // ── Reduction knob (-60 dB – 0 dB) ──
    reductionSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 30);
    ConjusKnobLookAndFeel::setKnobType(reductionSlider, KnobType::Reduction);
    reductionSlider.setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    addAndMakeVisible(reductionSlider);

    // ── Labels ──
    sensitivityLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(sensitivityLabel);

    reductionLabel.setJustificationType(juce::Justification::centredBottom);
    addAndMakeVisible(reductionLabel);

    // Ensure the knob sliders paint IN FRONT of their column labels. With the
    // knob centred at the window's vertical midpoint, the top of the ring
    // extends upward into the bounds of the "SENSITIVITY"/"REDUCTION" labels;
    // without this, the label text would overpaint the bright hover arc.
    sensitivityLabel.toBack();
    reductionLabel.toBack();

    // ── Attachments ──
    sensitivityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "sensitivity", sensitivitySlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "reduction", reductionSlider);

    // Set text functions AFTER attachment
    sensitivitySlider.textFromValueFunction = [](double value) -> juce::String {
        return juce::String(value, 1);
    };
    sensitivitySlider.valueFromTextFunction = [](const juce::String& text) -> double {
        return text.getDoubleValue();
    };
    sensitivitySlider.updateText();

    // Parameter is stored as positive attenuation (0 – 60). Display as a
    // negative dB value so the user sees "-30 dB" in the pill / text box.
    reductionSlider.textFromValueFunction = [](double value) -> juce::String {
        if (value <= 0.0)
            return "0.0 dB";
        return "-" + juce::String(value, 1) + " dB";
    };
    reductionSlider.valueFromTextFunction = [](const juce::String& text) -> double {
        return std::abs(text.getDoubleValue());
    };
    reductionSlider.updateText();

    // ── Animated snap to default on double-click ──
    sensitivitySlider.onDoubleClick = [this]() {
        startSnapAnimation(sensitivitySlider, sensitivityAnim);
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
            learnButton.setButtonText("START");
            learnButton.getProperties().set("stateTarget", 0.0);
        }
        else
        {
            processorRef.startLearning();
            learnButton.setButtonText("STOP");
            learnButton.getProperties().set("stateTarget", 1.0);
        }
    };
    learnButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(learnButton);

    // Bypass button — circular power switch in the top-right corner. Click
    // toggles the APVTS bool parameter, which processBlock early-returns
    // on so the DSP is fully pass-through when bypassed.
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.getAPVTS(), "bypass", bypassButton);

    // Mode button — styled identically to the Learn pill (same font,
    // same LAF, same hover + state-progress properties). Click toggles
    // the "hq" APVTS parameter; the pill crossfades "Realtime" → "HQ"
    // via the PillButtonLAF.
    modeButton.setClickingTogglesState(true);
    modeButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    modeButton.setConnectedEdges(0);
    modeButton.getProperties().set("stateTarget",
        processorRef.isHQMode() ? 1.0 : 0.0);
    modeButton.getProperties().set("stateProgress",
        processorRef.isHQMode() ? 1.0 : 0.0);
    modeButton.onStateChange = [this]() {
        modeButton.getProperties().set("stateTarget",
            modeButton.getToggleState() ? 1.0 : 0.0);
    };
    addAndMakeVisible(modeButton);
    modeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.getAPVTS(), "hq", modeButton);

    // ── Spectrum graph ──
    spectrumGraph.setFftSize(processorRef.getFFTSize());
    spectrumGraph.setSampleRate(processorRef.getDspSampleRate() > 0.0
                                    ? processorRef.getDspSampleRate()
                                    : 44100.0);
    spectrumGraph.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(spectrumGraph);

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
    setResizable(true, false); // we provide our own, larger corner
    setResizeLimits(KnobDesign::minWidth, KnobDesign::minHeight,
                    KnobDesign::maxWidth, KnobDesign::maxHeight);
    resizer = std::make_unique<juce::ResizableCornerComponent>(this, getConstrainer());
    resizer->setLookAndFeel(&conjusLAF);
    addAndMakeVisible(resizer.get());
    getConstrainer()->setFixedAspectRatio(
        static_cast<double>(KnobDesign::defaultWidth) / KnobDesign::defaultHeight);
    // setSize() must come AFTER the resizer is created — it triggers resized()
    // which positions the resizer. Before this reorder, resized() ran while
    // `resizer` was still null, so the handle stayed at 0x0 until the first
    // user-driven resize.
    setSize(savedW, savedH);

    // Receive mouse events from self and child components (for conjius logo hover)
    addMouseListener(this, true);

    startTimerHz(60);
}

TiptoeAudioProcessorEditor::~TiptoeAudioProcessorEditor()
{
    if (resizer) resizer->setLookAndFeel(nullptr);
    // Clear the per-instance LAFs we attached via raw pointers so
    // ~TextButton doesn't call back into freed memory.
    learnButton.setLookAndFeel(nullptr);
    modeButton .setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
    stopTimer();
}

void TiptoeAudioProcessorEditor::setChromeVisible(bool visible)
{
    // Used by the headless screenshot tool that generates the README /
    // release-page image: hide the conjius logo, the latency label, AND
    // the bypass button so the screenshot captures the bare plugin UI.
    // The MODE button stays visible — it's part of the core controls, not
    // chrome, and the "MODE" text label is drawn unconditionally in paint().
    showChrome = visible;
    latencyLabel.setVisible(visible);
    latencyHitArea.setVisible(visible);
    bypassButton.setVisible(visible);
    repaint();
}

void TiptoeAudioProcessorEditor::refreshSpectrumGraph()
{
    const double dspRate = processorRef.getDspSampleRate();
    if (dspRate > 0.0)
        spectrumGraph.setSampleRate(dspRate);

    const float thr = processorRef.getAPVTS()
                          .getRawParameterValue("sensitivity")->load();
    spectrumGraph.setSensitivityMultiplier(thr);

    processorRef.copyInputMagnitudes(scratchInputMags);
    processorRef.copyNoiseProfile(scratchNoiseMags);
    spectrumGraph.setSnapshot(scratchNoiseMags, scratchInputMags);
    spectrumGraph.repaint();
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
    // Update latency text only every 12th frame (~5 Hz) so the number is comfortably readable.
    // Shows ALGORITHMIC latency (the FFT-buffering delay the DAW compensates
    // for) — not the wall-clock processBlock time.
    if (++latencyTick >= 12)
    {
        latencyTick = 0;
        float ms = processorRef.getAlgorithmicLatencyMs();
        latencyLabel.setText("LATENCY: " + juce::String(ms, 1) + "ms",
                             juce::dontSendNotification);
    }

    // Pump the spectrum graph with the latest noise profile + live input
    // snapshot at the full 60 Hz timer rate. Smoothness comes from the
    // graph's time-domain exponential smoother (low alpha), not from
    // sampling less often — so the curve keeps a responsive feel while
    // easing between frames.
    {
        const double dspRate = processorRef.getDspSampleRate();
        if (dspRate > 0.0)
            spectrumGraph.setSampleRate(dspRate);

        const float thr = processorRef.getAPVTS()
                              .getRawParameterValue("sensitivity")->load();
        spectrumGraph.setSensitivityMultiplier(thr);

        processorRef.copyInputMagnitudes(scratchInputMags);
        processorRef.copyNoiseProfile(scratchNoiseMags);
        spectrumGraph.setSnapshot(scratchNoiseMags, scratchInputMags);
    }

    // Animate conjius logo hover state
    float target = logoHoverTarget ? 1.0f : 0.0f;
    if (std::abs(target - logoHoverProgress) > 0.002f)
    {
        logoHoverProgress += (target - logoHoverProgress) * 0.18f;
        repaint(logoBounds.expanded(static_cast<int>(logoBounds.getWidth() * 0.2f)));
    }

    // Animate hover colour interpolation for knobs and Learn button
    auto animateHover = [](juce::Component& c, bool hovered) {
        float current = static_cast<float>(c.getProperties().getWithDefault("hoverProgress", 0.0));
        float dest = hovered ? 1.0f : 0.0f;
        if (std::abs(dest - current) > 0.002f)
        {
            current += (dest - current) * 0.22f;
            c.getProperties().set("hoverProgress", current);
            c.repaint();
        }
    };
    animateHover(sensitivitySlider, sensitivitySlider.isMouseOverOrDragging(true));
    animateHover(reductionSlider, reductionSlider.isMouseOverOrDragging(true));
    animateHover(learnButton,     learnButton.isOver() || learnButton.isDown());
    animateHover(modeButton,      modeButton .isOver() || modeButton .isDown());

    // Update snap-to-default animations
    updateSnapAnimation(sensitivitySlider, sensitivityAnim);
    updateSnapAnimation(reductionSlider, reductionAnim);

    // Update learn button state (in case processor state changed externally)
    if (processorRef.isLearning() && learnButton.getButtonText() != "STOP")
    {
        learnButton.setButtonText("STOP");
        learnButton.getProperties().set("stateTarget", 1.0);
    }
    else if (!processorRef.isLearning() && learnButton.getButtonText() != "START")
    {
        learnButton.setButtonText("START");
        learnButton.getProperties().set("stateTarget", 0.0);
    }

    // Animate Learn button visual state transition (Start <-> Stop)
    // AND the Mode button transition (Realtime <-> HQ). Same one-pole
    // lerp toward the current stateTarget on each button's properties.
    auto advanceStateProgress = [](juce::Button& b) {
        auto& props = b.getProperties();
        const float stateDest = static_cast<float>(props.getWithDefault("stateTarget",   0.0));
        float       current   = static_cast<float>(props.getWithDefault("stateProgress", 0.0));
        if (std::abs(stateDest - current) > 0.002f)
        {
            current += (stateDest - current) * 0.20f;
            props.set("stateProgress", current);
            b.repaint();
        }
    };
    advanceStateProgress(learnButton);
    advanceStateProgress(modeButton);

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
            latencyHideProgress += (hideDest - latencyHideProgress) * 0.09f;

        if (!latencyBaseBounds.isEmpty())
        {
            float scale = 1.0f + 0.4f * latencyHoverProgress; // 1.0 → 1.4x
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

    // Draw conjius logo in bottom-left corner — tight to the bottom edge with
    // only a small horizontal pad from the left. Darker by default, brightens
    // and grows on hover.
    if (logoImage.isValid() && showChrome)
    {
        float scale = static_cast<float>(getWidth()) / static_cast<float>(KnobDesign::defaultWidth);
        int baseSize = static_cast<int>(37.5f * scale);
        int padLeft = static_cast<int>(6.0f * scale);
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
    const float hTotal = static_cast<float>(getHeight());
    // Match resized(): knob-area ratios are relative to the sub-window
    // beneath the spectrum graph. Add graphH + topPad when translating to
    // editor Y — topPad is the fixed gap between the graph's bottom edge
    // and the knob column labels.
    const float graphH = hTotal * KnobDesign::graphAreaFrac;
    const float topPad = KnobDesign::kKnobAreaTopPadPx
                         * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
    float h = hTotal - graphH - topPad;
    const float knobAreaY0 = graphH + topPad;

    // Draw title logo at top-centre (small)
    if (titleLogoImage.isValid())
    {
        float titleH = h * 0.190f; // ~1.3× the previous title height
        float aspect = static_cast<float>(titleLogoImage.getWidth())
                     / static_cast<float>(titleLogoImage.getHeight());
        float titleW = titleH * aspect;
        float titleX = (w - titleW) * 0.5f;
        // Raise logo by 50 px at the default window height — scaled so
        // the same visual shift applies across resizes.
        const float kLogoShiftUp = 50.0f * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
        float titleY = knobAreaY0 + h * 0.24f - kLogoShiftUp;
        g.drawImage(titleLogoImage,
                    juce::Rectangle<float>(titleX, titleY, titleW, titleH),
                    juce::RectanglePlacement::centred);

        // Subtitle: "Realtime denoiser" tagline diagonally below/right of the logo
        float subFontSize = h * 0.036f; // ~1.3× to match the logo bump
        auto subFont = conjusLAF.getBoldFont(subFontSize);
        g.setColour(KnobDesign::accentHoverColour);
        g.setFont(subFont);
        // Two-line, left-aligned subtitle with the rect sized exactly to the
        // widest line so the "container" matches the text's actual width.
        const float subW = KnobDesign::stringWidth(subFont, "DENOISER");
        float subX = titleX + titleW * 0.48f;
        float subY = titleY + titleH * 0.70f;
        const float subLineH = subFontSize * 1.1f;
        g.drawText("REALTIME",
                   juce::Rectangle<float>(subX, subY, subW, subFontSize * 1.4f),
                   juce::Justification::topLeft, false);
        g.drawText("DENOISER",
                   juce::Rectangle<float>(subX, subY + subLineH, subW, subFontSize * 1.4f),
                   juce::Justification::topLeft, false);
    }

    // Draw "Learn" / "Learning..." label above the button — size matches knob tick labels
    float textBoxH_est = w * KnobDesign::dbTextScale * 2.6f;
    float sliderBoundsW_est = w * 0.40f * 0.90f;
    auto tsBounds = sensitivitySlider.getBounds();
    float knobAreaH_est = tsBounds.isEmpty()
        ? (h * 0.96f - (h * 0.05f + 50.0f * (h / static_cast<float>(KnobDesign::defaultHeight))
                        + h * 0.14f + h * 0.09f)) - textBoxH_est
        : static_cast<float>(tsBounds.getHeight()) - textBoxH_est;
    float learnDiameter = juce::jmin(sliderBoundsW_est, knobAreaH_est) * 0.78f;
    float learnFontSize = learnDiameter * KnobDesign::labelFontScale
                          * KnobDesign::learnTextFontScaleFactor();

    auto labelFontLearn = conjusLAF.getBoldFont(learnFontSize);
    g.setFont(labelFontLearn);

    // Centre column between the two knobs — text sits just above the button
    float centreX = w * 0.5f;
    float btnH = h * 0.07f;
    // Learn button raised slightly (0.72 → 0.68) so it sits closer to the
    // MODE pair above.
    float btnY = h * 0.68f - btnH * 0.5f + knobAreaY0;
    float labelY = btnY - learnFontSize * 1.25f; // closer to the START/STOP button

    // Crossfade alphas between "Learn" and "Learning" (without the dots)
    float alphaLearn    = juce::jmax(0.0f, 1.0f - 2.0f * learningTextProgress);
    float alphaLearning = juce::jmax(0.0f, 2.0f * learningTextProgress - 1.0f);

    // "Learn" (static, centred)
    if (alphaLearn > 0.001f)
    {
        g.setColour(KnobDesign::accentColour.withMultipliedAlpha(alphaLearn));
        g.drawText("LEARN",
                   juce::Rectangle<float>(centreX - w * 0.15f, labelY, w * 0.3f, learnFontSize * 1.2f),
                   juce::Justification::centred, false);
    }

    // "Learning" + animated dots
    if (alphaLearning > 0.001f)
    {
        float learningW = KnobDesign::stringWidth(labelFontLearn, "LEARNING");
        float dotW      = KnobDesign::stringWidth(labelFontLearn, ".");
        float dotSpacing = dotW * 0.55f; // tighter than the glyph advance
        float fullW     = learningW + 3.0f * dotSpacing;

        float baseX = centreX - fullW * 0.5f;
        auto colour = KnobDesign::accentColour.withMultipliedAlpha(alphaLearning);

        g.setColour(colour);
        g.drawText("LEARNING",
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

    // "MODE" label drawn directly above the MODE pill, mirroring the
    // "LEARN" label placement relative to the learn pill. Static text
    // (no crossfade), same font, same colour, same vertical spacing.
    {
        // Mode button is positioned pairStride samples above the learn
        // button; the same formula applies to the text labels. pairStride
        // is recomputed to match resized().
        const float pairStride = btnH + h * 0.055f;
        // Reduced from 50 → 15 px: mode pair now sits closer to the LEARN
        // pair (user asked to move mode down + learn up). The two are still
        // clearly separated by a full pairStride.
        const float kModeShiftUp = 15.0f * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
        const float modeLabelY = labelY - pairStride - kModeShiftUp;
        g.setFont(labelFontLearn);
        g.setColour(KnobDesign::accentColour);
        g.drawText("MODE",
                   juce::Rectangle<float>(centreX - w * 0.15f, modeLabelY,
                                          w * 0.3f, learnFontSize * 1.2f),
                   juce::Justification::centred, false);
    }

    // Border is drawn in paintOverChildren() so it sits on top of every
    // child component, including the spectrum graph's curves.
}

void TiptoeAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    const float scaleF  = static_cast<float>(getWidth())
                        / static_cast<float>(KnobDesign::defaultWidth);
    const float pad     = 30.0f * scaleF;
    const float borderW = 4.0f  * scaleF;
    // Match the knob ring's drawn radius (see drawRotarySlider — diameter
    // caps at sliderW*0.60, so at default width 650 knobRadius = sliderW/2
    // = knobColW*0.90/2 * 0.60 ≈ 70 px). Keep border corner radius identical
    // so the outer rounded rectangle shares the knobs' curvature.
    const float radius  = 70.0f * scaleF;
    juce::Rectangle<float> borderRect{ pad, pad,
                                       static_cast<float>(getWidth())  - 2.0f * pad,
                                       static_cast<float>(getHeight()) - 2.0f * pad };
    juce::Path border;
    border.addRoundedRectangle(borderRect, radius);
    g.setColour(KnobDesign::accentColour);
    g.strokePath(border, juce::PathStrokeType(borderW));
}

void TiptoeAudioProcessorEditor::resized()
{
    processorRef.editorWidth.store(getWidth());
    processorRef.editorHeight.store(getHeight());

    if (resizer != nullptr)
    {
        const int handleSize = 28; // bigger than JUCE's 16 default
        resizer->setBounds(getWidth() - handleSize, getHeight() - handleSize,
                           handleSize, handleSize);
        resizer->toFront(false);
        resizer->repaint(); // force initial paint so handle is visible at rest
    }

    float w = static_cast<float>(getWidth());
    const float hTotal = static_cast<float>(getHeight());

    // Spectrum graph fills the top slice of the window, inset slightly so
    // it sits inside the orange border.
    const float pad = 30.0f * (w / static_cast<float>(KnobDesign::defaultWidth));
    const float graphH = hTotal * KnobDesign::graphAreaFrac;
    spectrumGraph.setBounds(static_cast<int>(pad),
                            static_cast<int>(pad),
                            static_cast<int>(w - 2.0f * pad),
                            static_cast<int>(graphH - pad));
    // Match the outer orange border radius so the graph content clips
    // against the same arc the border traces. Same 78 × scale formula used
    // to draw the border in paint().
    {
        const float scaleF = w / static_cast<float>(KnobDesign::defaultWidth);
        spectrumGraph.setCornerRadius(70.0f * scaleF);
        // Keep in sync with the border stroke drawn in paintOverChildren().
        spectrumGraph.setBorderStrokeWidth(4.0f * scaleF);
    }

    // Bypass button — sits OUTSIDE the orange border, tucked into the
    // window's top-right corner with a small gap from both edges. Because
    // the border is drawn via paintOverChildren() and the button lives
    // in the corner area outside the border's rounded rect, the border
    // stroke naturally passes underneath the button without overlapping.
    {
        const float scaleF  = w / static_cast<float>(KnobDesign::defaultWidth);
        // Matches the button-to-border arc diagonal gap at pad=30, radius=70,
        // btnSize=34 (computed: √2·(pad+radius − edgeGap − btnSize/2) − radius
        // − btnSize/2·√2 ≈ 10 px). So button-to-edge = button-to-border.
        const float edgeGap = 10.0f * scaleF;
        const float btnSize = 34.0f * scaleF;
        const float btnX = static_cast<float>(getWidth()) - edgeGap - btnSize;
        const float btnY = edgeGap;
        bypassButton.setBounds(static_cast<int>(btnX),
                               static_cast<int>(btnY),
                               static_cast<int>(btnSize),
                               static_cast<int>(btnSize));
        // Guarantee the button sits on top of every sibling — the spectrum
        // graph, the orange border (drawn via paintOverChildren), the
        // knobs. Belt-and-braces against any future child added below.
        bypassButton.toFront(false);
    }

    // All knob-area positioning below works in the SUB-window beneath the
    // graph + a fixed top pad. Keep `h` as the remaining height so the
    // existing ratios still map the knob/button/pill the way they used to;
    // each setBounds()/paint Y gets `knobAreaY0` (= graphH + topPad) added
    // back to translate into editor coords.
    const float topPad = KnobDesign::kKnobAreaTopPadPx
                         * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
    const float knobAreaY0 = graphH + topPad;
    float h = hTotal - graphH - topPad;

    float margin = w * 0.05f;

    // Two knob columns: left 40%, right 40%, centre 20%
    float knobColW = w * 0.40f;
    float centreColW = w * 0.20f;
    float knobColX0 = margin;
    float knobColX1 = w - margin - knobColW;

    // ── Parameter labels ──
    // Smaller font and positioned just below the graph area, above the
    // mid-tick label of each knob. The previous larger label collided with
    // the Reduction knob's centred "-30" top-tick label.
    const float labelFontSize = KnobDesign::columnLabelFontSize(w);
    auto labelFont = conjusLAF.getBoldFont(labelFontSize);
    sensitivityLabel.setFont(labelFont);
    reductionLabel.setFont(labelFont);

    const int labelH = static_cast<int>(KnobDesign::columnLabelHeight(w));
    const int labelY = static_cast<int>(knobAreaY0 + h * KnobDesign::columnLabelTopYInKnobArea());
    sensitivityLabel.setBounds(static_cast<int>(knobColX0), labelY,
                             static_cast<int>(knobColW), labelH);
    reductionLabel.setBounds(static_cast<int>(knobColX1), labelY,
                             static_cast<int>(knobColW), labelH);

    // ── Knob sliders ──
    // Expand slider bounds so the knob (centred at the knob-area's midpoint)
    // is drawn entirely INSIDE the slider's own bounds — avoids the hover
    // highlight being clipped by sibling components / parent clip regions.
    float dbFontSize = w * KnobDesign::dbTextScale;
    int sliderBottom = static_cast<int>(h * 0.96f);
    int sliderTop = static_cast<int>(h) - sliderBottom; // symmetric around h/2
    int sliderH = sliderBottom - sliderTop;

    // Translate slider Y into editor coordinates, with an extra tiny drop so
    // the value pills sit a bit lower on the page (matches the knob-ring
    // shift in drawRotarySlider, keeping the whole knob cluster aligned).
    const float knobClusterExtraShift = 20.0f * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
    const int sliderTopEditor = sliderTop + static_cast<int>(knobAreaY0 + knobClusterExtraShift);

    // Tighten slider bounds to match visible knob area
    float sliderBoundsW = knobColW * 0.90f;
    float sliderOffset0 = knobColX0 + (knobColW - sliderBoundsW) * 0.5f;
    float sliderOffset1 = knobColX1 + (knobColW - sliderBoundsW) * 0.5f;

    int textBoxW = static_cast<int>(sliderBoundsW * 0.95f);
    int textBoxH = static_cast<int>(dbFontSize * 2.6f);

    sensitivitySlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);
    sensitivitySlider.setMouseDragSensitivity(static_cast<int>(w * 0.5f));
    sensitivitySlider.setBounds(static_cast<int>(sliderOffset0), sliderTopEditor,
                              static_cast<int>(sliderBoundsW), sliderH);

    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textBoxW, textBoxH);
    reductionSlider.setMouseDragSensitivity(static_cast<int>(w * 0.5f));
    reductionSlider.setBounds(static_cast<int>(sliderOffset1), sliderTopEditor,
                              static_cast<int>(sliderBoundsW), sliderH);

    // Update text box fonts and allow pills to paint above label bounds
    for (auto* slider : { &sensitivitySlider, &reductionSlider })
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
    // Raised from h*0.72 → h*0.68 so the learn pair sits a bit closer to
    // the mode pair above. Must mirror the same move in paint().
    float btnY = h * 0.68f - btnH * 0.5f + knobAreaY0;

    learnButton.setBounds(static_cast<int>(btnX), static_cast<int>(btnY),
                          static_cast<int>(btnW), static_cast<int>(btnH));

    // ── Mode button pair (MODE text + pill) — same size / font / pill
    //    LAF as the learn pair. Sits ABOVE the learn pair in the centre
    //    column (mode button + label → learn button + label, top to
    //    bottom). The whole stack is nudged lower on the page so there
    //    is comfortable breathing room above the mode label.
    //    The MODE pill's button-text is set from the APVTS "hq" state
    //    in the constructor (callback) and in the timer callback so it
    //    tracks external automation. ──
    float modeBtnW = btnW;
    float modeBtnH = btnH;
    float modeBtnX = btnX;
    // Mode button sits one "pair height" above the learn button (so
    // its "MODE" label has the same spacing above it as "LEARN" does).
    float pairStride = btnH + h * 0.055f;
    // Reduced 50 → 15 px so the mode pair sits closer to the learn pair.
    // Mirror this in paint() (both values must stay in sync).
    const float kModeShiftUp = 15.0f * (hTotal / static_cast<float>(KnobDesign::defaultHeight));
    float modeBtnY   = btnY - pairStride - kModeShiftUp;

    modeButton.setBounds(static_cast<int>(modeBtnX), static_cast<int>(modeBtnY),
                         static_cast<int>(modeBtnW), static_cast<int>(modeBtnH));
    modeButton.setConnectedEdges(0);

    // Pill with fully circular side edges + larger text
    float btnFontSize = KnobDesign::learnButtonFontSize(w);
    learnButton.setConnectedEdges(0);

    // Custom LookAndFeel for pill shape button. Shared by both pairs
    // (learn + mode) — the only thing that differs between them is the
    // pair of text labels (Start/Stop vs Realtime/HQ).
    struct PillButtonLAF : juce::LookAndFeel_V4
    {
        juce::Font font;
        float knobStrokeW;
        juce::String offText;
        juce::String onText;
        PillButtonLAF(juce::Font f, float ksw,
                      juce::String off, juce::String on)
            : font(f), knobStrokeW(ksw), offText(off), onText(on) {}

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
                g.drawText(offText, bounds, juce::Justification::centred, false);
            }
            if (alphaStop > 0.001f)
            {
                g.setColour(textColour.withMultipliedAlpha(alphaStop));
                g.drawText(onText, bounds, juce::Justification::centred, false);
            }
        }
    };
    static PillButtonLAF* pillLAF     = nullptr;
    static PillButtonLAF* modePillLAF = nullptr;
    delete pillLAF;
    delete modePillLAF;
    // Compute knob stroke width in pixels to match actual knob border thickness.
    // Use the same diameter formula as drawRotarySlider: jmin(sliderW, knobAreaH) * 0.78.
    float knobAreaH = static_cast<float>(sliderH) - static_cast<float>(textBoxH);
    float knobDiameter = juce::jmin(sliderBoundsW, knobAreaH) * 0.78f;
    float knobStrokeW = knobDiameter * KnobDesign::knobStrokeFrac;
    pillLAF     = new PillButtonLAF(conjusLAF.getBoldFont(btnFontSize), knobStrokeW, "START",    "STOP");
    modePillLAF = new PillButtonLAF(conjusLAF.getBoldFont(btnFontSize), knobStrokeW, "REALTIME", "HQ");
    learnButton.setLookAndFeel(pillLAF);
    modeButton .setLookAndFeel(modePillLAF);

    // Pass the exact knob-stroke thickness to the bypass button so its
    // bypassed-state ring reads as part of the same visual family as the
    // knob outlines. This does NOT change the button's size or the
    // centred power glyph — the ring is stroked inside the disc bounds.
    bypassButton.setRingStrokeWidth(knobStrokeW);

    // ── Latency label ──
    float latencyFontSize = w * KnobDesign::latencyTextScale;
    latencyLabel.setFont(conjusLAF.getRegularFont(latencyFontSize));
    latencyLabel.setJustificationType(juce::Justification::centredBottom);
    int latencyH = static_cast<int>(latencyFontSize * 2.0f);
    // Sit clearly above the bottom edge (peek position is the same since
    // the peek animation just eases hideProgress back to 0).
    int latencyLift = static_cast<int>(latencyFontSize * 3.0f);
    latencyBaseBounds = { 0, getHeight() - latencyH - latencyLift, getWidth(), latencyH };
    latencyBaseFontSize = latencyFontSize;
    // Hit area: narrow — matches the actual text width with a small horizontal pad
    auto latencyFont = conjusLAF.getRegularFont(latencyFontSize);
    int textW = static_cast<int>(KnobDesign::stringWidth(latencyFont, "LATENCY: 0.000ms"));
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
        &slider == &sensitivitySlider ? "sensitivity" : "reduction");
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
