#include "PluginEditor.h"

DenoiserAudioProcessorEditor::DenoiserAudioProcessorEditor(DenoiserAudioProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    thresholdSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    thresholdSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible(thresholdSlider);

    reductionSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    reductionSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    addAndMakeVisible(reductionSlider);

    thresholdLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(thresholdLabel);

    reductionLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(reductionLabel);

    learnButton.onClick = [this]()
    {
        if (processorRef.isLearning())
        {
            processorRef.stopLearning();
            learnButton.setButtonText("Learn Noise");
            learnButton.setColour(juce::TextButton::buttonColourId, getLookAndFeel().findColour(juce::TextButton::buttonColourId));
        }
        else
        {
            processorRef.startLearning();
            learnButton.setButtonText("Learning...");
            learnButton.setColour(juce::TextButton::buttonColourId, juce::Colours::red);
        }
    };
    addAndMakeVisible(learnButton);

    latencyLabel.setJustificationType(juce::Justification::centredLeft);
    latencyLabel.setFont(juce::Font(12.0f));
    latencyLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(latencyLabel);

    thresholdAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "threshold", thresholdSlider);
    reductionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "reduction", reductionSlider);

    setSize(400, 300);
    startTimerHz(30);
}

DenoiserAudioProcessorEditor::~DenoiserAudioProcessorEditor()
{
    stopTimer();
}

void DenoiserAudioProcessorEditor::timerCallback()
{
    float ms = processorRef.getLastProcessingTimeMs();
    latencyLabel.setText(juce::String::formatted("Latency: %.3fms", ms),
                         juce::dontSendNotification);
}

void DenoiserAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a2e));
    g.setColour(juce::Colours::white);
    g.setFont(22.0f);
    g.drawFittedText("Denoiser", getLocalBounds().removeFromTop(40), juce::Justification::centred, 1);
}

void DenoiserAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(30); // title space

    auto knobArea = area.removeFromTop(180);
    auto knobWidth = knobArea.getWidth() / 2;

    auto leftKnob = knobArea.removeFromLeft(knobWidth);
    thresholdLabel.setBounds(leftKnob.removeFromTop(20));
    thresholdSlider.setBounds(leftKnob);

    auto rightKnob = knobArea;
    reductionLabel.setBounds(rightKnob.removeFromTop(20));
    reductionSlider.setBounds(rightKnob);

    learnButton.setBounds(area.removeFromTop(40).reduced(80, 5));

    latencyLabel.setBounds(getLocalBounds().removeFromBottom(20).removeFromLeft(200).withTrimmedLeft(10));
}
