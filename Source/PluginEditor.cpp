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
    thresholdSlider.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(thresholdSlider);

    // ── Reduction knob (-60 dB – 0 dB) ──
    reductionSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 120, 30);
    ConjusKnobLookAndFeel::setKnobType(reductionSlider, KnobType::Reduction);
    reductionSlider.setMouseCursor(juce::MouseCursor::PointingHandCursor);
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
    learnButton.setColour(juce::TextButton::buttonColourId, KnobDesign::accentColour);
    learnButton.setColour(juce::TextButton::textColourOffId, KnobDesign::bgColour);
    learnButton.onClick = [this]()
    {
        if (processorRef.isLearning())
        {
            processorRef.stopLearning();
            learnButton.setButtonText("Start");
            learnButton.setColour(juce::TextButton::buttonColourId, KnobDesign::accentColour);
            learnButton.setColour(juce::TextButton::textColourOffId, KnobDesign::bgColour);
        }
        else
        {
            processorRef.startLearning();
            learnButton.setButtonText("Stop");
            learnButton.setColour(juce::TextButton::buttonColourId, KnobDesign::bgColour);
            learnButton.setColour(juce::TextButton::textColourOffId, KnobDesign::accentColour);
        }
    };
    learnButton.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(learnButton);

    // ── Latency label ──
    latencyLabel.setJustificationType(juce::Justification::centredLeft);
    latencyLabel.setColour(juce::Label::textColourId, KnobDesign::accentColour.darker(0.3f));
    addAndMakeVisible(latencyLabel);

    // ── Logo ──
    logoImage = juce::ImageCache::getFromMemory(
        BinaryData::conjiusavatartransparentbg_png,
        BinaryData::conjiusavatartransparentbg_pngSize);

    // ── Window ──
    int savedW = processorRef.editorWidth.load();
    int savedH = processorRef.editorHeight.load();
    setSize(savedW, savedH);
    setResizable(true, true);
    setResizeLimits(KnobDesign::minWidth, KnobDesign::minHeight,
                    KnobDesign::maxWidth, KnobDesign::maxHeight);
    getConstrainer()->setFixedAspectRatio(
        static_cast<double>(KnobDesign::defaultWidth) / KnobDesign::defaultHeight);

    startTimerHz(60);
}

TiptoeAudioProcessorEditor::~TiptoeAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void TiptoeAudioProcessorEditor::timerCallback()
{
    float ms = processorRef.getLastProcessingTimeMs();
    latencyLabel.setText("Latency: " + juce::String(ms, 3) + "ms",
                         juce::dontSendNotification);

    // Update snap-to-default animations
    updateSnapAnimation(thresholdSlider, thresholdAnim);
    updateSnapAnimation(reductionSlider, reductionAnim);

    // Update learn button state (in case processor state changed externally)
    if (processorRef.isLearning() && learnButton.getButtonText() != "Stop")
    {
        learnButton.setButtonText("Stop");
        learnButton.setColour(juce::TextButton::buttonColourId, KnobDesign::bgColour);
        learnButton.setColour(juce::TextButton::textColourOffId, KnobDesign::accentColour);
    }
    else if (!processorRef.isLearning() && learnButton.getButtonText() != "Start")
    {
        learnButton.setButtonText("Start");
        learnButton.setColour(juce::TextButton::buttonColourId, KnobDesign::accentColour);
        learnButton.setColour(juce::TextButton::textColourOffId, KnobDesign::bgColour);
    }
}

void TiptoeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(KnobDesign::bgColour);

    // Draw logo in bottom-left corner
    if (logoImage.isValid())
    {
        float scale = static_cast<float>(getWidth()) / static_cast<float>(KnobDesign::defaultWidth);
        int logoSize = static_cast<int>(37.5f * scale);
        int padLeft = logoSize / 3;
        g.drawImage(logoImage,
                    padLeft, getHeight() - logoSize, logoSize, logoSize,
                    0, 0, logoImage.getWidth(), logoImage.getHeight());
    }

    // Draw "Learn" label above the button
    float w = static_cast<float>(getWidth());
    float learnFontSize = w * KnobDesign::gainLabelScale / 2.5f * 1.3f;
    g.setColour(KnobDesign::accentColour);
    g.setFont(conjusLAF.getBoldFont(learnFontSize));

    // Centre column between the two knobs, closer to button
    float centreX = w * 0.5f;
    float labelY = static_cast<float>(getHeight()) * 0.42f + 50.0f * (static_cast<float>(getHeight()) / static_cast<float>(KnobDesign::defaultHeight));
    g.drawText("Learn",
               juce::Rectangle<float>(centreX - w * 0.15f, labelY, w * 0.3f, learnFontSize * 1.2f),
               juce::Justification::centred, false);
}

void TiptoeAudioProcessorEditor::resized()
{
    processorRef.editorWidth.store(getWidth());
    processorRef.editorHeight.store(getHeight());

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float margin = w * 0.05f;

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
    int labelY = static_cast<int>(h * 0.05f + 50.0f * (h / static_cast<float>(KnobDesign::defaultHeight)));
    thresholdLabel.setBounds(static_cast<int>(knobColX0), labelY,
                             static_cast<int>(knobColW), labelH);
    reductionLabel.setBounds(static_cast<int>(knobColX1), labelY,
                             static_cast<int>(knobColW), labelH);

    // ── Knob sliders ──
    float dbFontSize = w * KnobDesign::dbTextScale;
    int sliderTop = labelY + labelH;
    int sliderBottom = static_cast<int>(h * 0.88f);
    int sliderH = sliderBottom - sliderTop;

    // Tighten slider bounds to match visible knob area
    float sliderBoundsW = knobColW * 0.70f;
    float sliderOffset0 = knobColX0 + (knobColW - sliderBoundsW) * 0.5f;
    float sliderOffset1 = knobColX1 + (knobColW - sliderBoundsW) * 0.5f;

    int textBoxW = static_cast<int>(sliderBoundsW * 0.95f);
    int textBoxH = static_cast<int>(dbFontSize * 2.0f);

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

    // ── Learn button (centred pill between knobs) ──
    float btnW = centreColW * 0.75f;
    float btnH = h * 0.07f;
    float btnX = w * 0.5f - btnW * 0.5f;
    float btnY = h * 0.5f - btnH * 0.2f + 50.0f * (h / static_cast<float>(KnobDesign::defaultHeight));

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
        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& backgroundColour,
                                  bool /*isMouseOver*/, bool /*isButtonDown*/) override
        {
            auto bounds = button.getLocalBounds().toFloat();
            float cornerR = bounds.getHeight() * 0.5f;
            bool isStopState = (backgroundColour == KnobDesign::bgColour);
            if (isStopState)
            {
                // Orange border, dark fill — stroke matches knob stroke width
                g.setColour(KnobDesign::bgColour);
                g.fillRoundedRectangle(bounds, cornerR);
                g.setColour(KnobDesign::accentColour);
                g.drawRoundedRectangle(bounds.reduced(knobStrokeW * 0.5f), cornerR, knobStrokeW);
            }
            else
            {
                g.setColour(backgroundColour);
                g.fillRoundedRectangle(bounds, cornerR);
            }
        }
        void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                            bool /*isMouseOver*/, bool /*isButtonDown*/) override
        {
            g.setFont(font);
            g.setColour(button.findColour(juce::TextButton::textColourOffId));
            g.drawText(button.getButtonText(), button.getLocalBounds().toFloat(),
                       juce::Justification::centred, false);
        }
    };
    static PillButtonLAF* pillLAF = nullptr;
    delete pillLAF;
    // Compute knob stroke width in pixels to match knob border thickness
    float knobDiameter = juce::jmin(sliderBoundsW, static_cast<float>(sliderH)) * 0.624f;
    float knobStrokeW = knobDiameter * KnobDesign::knobStrokeFrac;
    pillLAF = new PillButtonLAF(conjusLAF.getBoldFont(btnFontSize), knobStrokeW);
    learnButton.setLookAndFeel(pillLAF);

    // ── Latency label ──
    float latencyFontSize = w * KnobDesign::latencyTextScale;
    latencyLabel.setFont(conjusLAF.getRegularFont(latencyFontSize));
    latencyLabel.setJustificationType(juce::Justification::centredBottom);
    int latencyH = static_cast<int>(latencyFontSize * 2.0f);
    latencyLabel.setBounds(0, getHeight() - latencyH, getWidth(), latencyH);
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
