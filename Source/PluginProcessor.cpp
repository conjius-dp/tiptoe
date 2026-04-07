#include "PluginProcessor.h"
#include "PluginEditor.h"

DenoiserAudioProcessor::DenoiserAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

DenoiserAudioProcessor::~DenoiserAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DenoiserAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("threshold", 1), "Threshold",
        juce::NormalisableRange<float>(0.5f, 5.0f, 0.01f), 1.5f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reduction", 1), "Reduction",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -30.0f));

    return { params.begin(), params.end() };
}

const juce::String DenoiserAudioProcessor::getName() const { return JucePlugin_Name; }
bool DenoiserAudioProcessor::acceptsMidi() const { return false; }
bool DenoiserAudioProcessor::producesMidi() const { return false; }
bool DenoiserAudioProcessor::isMidiEffect() const { return false; }
double DenoiserAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int DenoiserAudioProcessor::getNumPrograms() { return 1; }
int DenoiserAudioProcessor::getCurrentProgram() { return 0; }
void DenoiserAudioProcessor::setCurrentProgram(int) {}
const juce::String DenoiserAudioProcessor::getProgramName(int) { return {}; }
void DenoiserAudioProcessor::changeProgramName(int, const juce::String&) {}

void DenoiserAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& d : denoisers)
        d.prepare(sampleRate, samplesPerBlock);
}

void DenoiserAudioProcessor::releaseResources()
{
    for (auto& d : denoisers)
        d.reset();
}

bool DenoiserAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void DenoiserAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Update parameters
    float threshold = apvts.getRawParameterValue("threshold")->load();
    float reduction = apvts.getRawParameterValue("reduction")->load();

    for (auto& d : denoisers)
    {
        d.setThreshold(threshold);
        d.setReduction(reduction);
    }

    int numSamples = buffer.getNumSamples();

    // Feed audio for noise learning if active
    if (learning_)
    {
        for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
            denoisers[ch].learnFromBlock(buffer.getReadPointer(ch), numSamples);
    }

    // Process each channel
    for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
        denoisers[ch].processMono(buffer.getWritePointer(ch), numSamples);
}

void DenoiserAudioProcessor::startLearning()
{
    learning_ = true;
    for (auto& d : denoisers)
        d.startLearning();
}

void DenoiserAudioProcessor::stopLearning()
{
    for (auto& d : denoisers)
        d.stopLearning();
    learning_ = false;
}

bool DenoiserAudioProcessor::isLearning() const
{
    return learning_;
}

float DenoiserAudioProcessor::getLastProcessingTimeMs() const
{
    return std::max(denoisers[0].getLastProcessingTimeMs(),
                    denoisers[1].getLastProcessingTimeMs());
}

juce::AudioProcessorEditor* DenoiserAudioProcessor::createEditor()
{
    return new DenoiserAudioProcessorEditor(*this);
}

bool DenoiserAudioProcessor::hasEditor() const { return true; }

void DenoiserAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void DenoiserAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DenoiserAudioProcessor();
}
