#pragma once

#include "PluginProcessor.h"

class DenoiserAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit DenoiserAudioProcessorEditor(DenoiserAudioProcessor&);
    ~DenoiserAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    DenoiserAudioProcessor& processorRef;

    juce::Slider thresholdSlider;
    juce::Slider reductionSlider;
    juce::TextButton learnButton { "Learn Noise" };

    juce::Label thresholdLabel { {}, "Threshold" };
    juce::Label reductionLabel { {}, "Reduction (dB)" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> reductionAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DenoiserAudioProcessorEditor)
};
