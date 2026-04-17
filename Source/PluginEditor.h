#pragma once

#include "PluginProcessor.h"
#include "KnobDesign.h"
#include "BinaryData.h"

// ── Slider subclass that delegates double-click to the editor ──
class AnimatedSlider : public juce::Slider
{
public:
    std::function<void()> onDoubleClick;

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (onDoubleClick)
            onDoubleClick();
        // Don't call Slider::mouseDoubleClick — we handle snap ourselves
    }

    // Tight hit-test: only the knob circle and the value-pill rectangle intercept mouse events.
    // Everywhere else within the slider's bounds, events pass through to the parent.
    bool hitTest(int x, int y) override
    {
        // Knob circle uses jmin(sliderW, knobAreaH) * 0.78 in drawRotarySlider;
        // approximate using slider local dimensions minus a textbox allowance.
        float sw = static_cast<float>(getWidth());
        float sh = static_cast<float>(getHeight());
        float textBoxH = sh * 0.25f; // ~matches editor layout ratio
        float knobAreaH = sh - textBoxH;
        float d = juce::jmin(sw, knobAreaH) * 0.78f;
        float r = d * 0.5f;
        float cx = sw * 0.5f;
        float cy = knobAreaH * 0.5f - d * 0.08f;
        float dx = static_cast<float>(x) - cx;
        float dy = static_cast<float>(y) - cy;
        if (dx * dx + dy * dy <= r * r) return true;

        // Pill rect — approx 60% width centred at the bottom text-box area
        int pillHalfW = static_cast<int>(sw * 0.30f);
        int pillTop    = static_cast<int>(sh - textBoxH * 0.85f);
        int pillBottom = static_cast<int>(sh - textBoxH * 0.05f);
        return (x >= static_cast<int>(cx) - pillHalfW
             && x <= static_cast<int>(cx) + pillHalfW
             && y >= pillTop
             && y <= pillBottom);
    }
};

class TiptoeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit TiptoeAudioProcessorEditor(TiptoeAudioProcessor&);
    ~TiptoeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    TiptoeAudioProcessor& processorRef;
    ConjusKnobLookAndFeel conjusLAF;

    AnimatedSlider thresholdSlider;
    AnimatedSlider reductionSlider;
    juce::Label thresholdLabel { {}, "Threshold" };
    juce::Label reductionLabel { {}, "Reduction" };
    juce::Label latencyLabel   { {}, "Latency: 0.000ms" };

    juce::TextButton learnButton { "Start" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;

    juce::Image logoImage;
    juce::Image titleLogoImage;

    // Conjius logo hover animation (darker -> bright + scale up on hover)
    juce::Rectangle<int> logoBounds;
    bool  logoHoverTarget   = false;
    float logoHoverProgress = 0.0f; // 0 = dim+normal size, 1 = bright + 1.2x

    // "Learn" / "Learning..." text crossfade + animated dots
    juce::Rectangle<int> learnTextBounds;
    float learningTextProgress = 0.0f; // 0 = "Learn", 1 = "Learning..."
    float dotAnimPhase         = 0.0f; // 0..1 loops while learning

    // Latency label hide/peek behaviour — click toggles hidden mode, hover peeks back in,
    // hover always grows text 3x.
    struct HitArea : juce::Component
    {
        std::function<void()> onClick;
        std::function<void(bool)> onHover;
        HitArea() {
            setInterceptsMouseClicks(true, false);
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        }
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
        void mouseEnter(const juce::MouseEvent&) override { if (onHover) onHover(true); }
        void mouseExit (const juce::MouseEvent&) override { if (onHover) onHover(false); }
    };
    HitArea latencyHitArea;
    juce::Rectangle<int> latencyBaseBounds;
    float latencyBaseFontSize  = 1.0f;
    bool  latencyHoverTarget   = false;
    float latencyHoverProgress = 0.0f;   // 0 = normal, 1 = 3x
    bool  latencyHidden        = false;  // toggled by click
    float latencyHideProgress  = 0.0f;   // 0 = visible, 1 = slid out of window

    // ── Snap-to-default animation ──
    struct SliderAnimation
    {
        bool active = false;
        double targetValue = 0.0;
        double currentValue = 0.0;
    };
    SliderAnimation thresholdAnim;
    SliderAnimation reductionAnim;

    void startSnapAnimation(juce::Slider& slider, SliderAnimation& anim);
    void updateSnapAnimation(juce::Slider& slider, SliderAnimation& anim);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiptoeAudioProcessorEditor)
};
