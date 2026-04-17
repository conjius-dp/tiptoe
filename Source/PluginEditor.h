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
