#pragma once

#include "PluginProcessor.h"

class TiptoeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                      private juce::Timer
{
public:
    explicit TiptoeAudioProcessorEditor(TiptoeAudioProcessor&);
    ~TiptoeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    TiptoeAudioProcessor& processorRef;

    juce::Slider thresholdSlider;
    juce::Slider reductionSlider;
    juce::TextButton learnButton { "Learn Noise" };

    juce::Label thresholdLabel { {}, "Threshold" };
    juce::Label reductionLabel { {}, "Reduction (dB)" };
    juce::Label latencyLabel { {}, "Latency: 0.000ms" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiptoeAudioProcessorEditor)
};
