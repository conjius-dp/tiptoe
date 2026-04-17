#pragma once

#include "PluginProcessor.h"
#include "KnobDesign.h"
#include "SpectrumGraph.h"
#include "BinaryData.h"

// ── Slider subclass that delegates double-click to the editor ──
class AnimatedSlider : public juce::Slider
{
public:
    std::function<void()> onDoubleClick;

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (onDoubleClick)
            onDoubleClick();
        // Don't call Slider::mouseDoubleClick — we handle snap ourselves
    }

    // Tight hit-test: only the knob circle and the value-pill rectangle intercept mouse events.
    // Mirrors the geometry inside drawRotarySlider() so dragging the knob
    // ring and double-clicking both stay accurate after the knob is shifted
    // down (see knobShiftDown in drawRotarySlider).
    bool hitTest(int x, int y) override
    {
        float sw = static_cast<float>(getWidth());
        float sh = static_cast<float>(getHeight());

        float parentH = sh;
        if (auto* editor = getParentComponent())
            parentH = static_cast<float>(editor->getHeight());
        const float knobShift = 70.0f * (parentH / static_cast<float>(KnobDesign::defaultHeight));

        float d = juce::jmin(juce::jmin(sw, sh) * 0.78f, sw * 0.60f);
        float r = d * 0.5f;
        float cx = sw * 0.5f;
        float cy = parentH * 0.5f - static_cast<float>(getY()) + knobShift;

        float dx = static_cast<float>(x) - cx;
        float dy = static_cast<float>(y) - cy;
        if (dx * dx + dy * dy <= r * r) return true;

        // Pill rect — bottom text-box area, unaffected by the knob shift.
        float textBoxH = sh * 0.25f;
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

    // Hide the conjius logo + latency label — used by the headless screenshot
    // tool so the README image doesn't include the footer chrome.
    void setChromeVisible(bool visible);

    // Sync the spectrum graph with the processor's current snapshots without
    // waiting for the timer. Used by the headless screenshot tool so the
    // graph is populated at the moment createComponentSnapshot() runs.
    void refreshSpectrumGraph();

private:
    bool showChrome = true;
    void timerCallback() override;

    TiptoeAudioProcessor& processorRef;
    ConjusKnobLookAndFeel conjusLAF;

    AnimatedSlider thresholdSlider;
    AnimatedSlider reductionSlider;
    juce::Label thresholdLabel { {}, "THRESHOLD" };
    juce::Label reductionLabel { {}, "REDUCTION" };
    juce::Label latencyLabel   { {}, "LATENCY: 0.000ms" };

    juce::TextButton learnButton { "START" };

    SpectrumGraph spectrumGraph;

    // Reused buffers for spectrum snapshots so we don't allocate per frame.
    std::vector<float> scratchInputMags;
    std::vector<float> scratchNoiseMags;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;

    juce::Image logoImage;
    juce::Image titleLogoImage;

    // Custom resize corner — larger than JUCE's default 16x16
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

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
