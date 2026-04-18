#include "PluginProcessor.h"
#include "PluginEditor.h"

TiptoeAudioProcessor::TiptoeAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameterLayout())
{
}

TiptoeAudioProcessor::~TiptoeAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout TiptoeAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("sensitivity", 1), "Sensitivity",
        juce::NormalisableRange<float>(0.1f, 3.0f, 0.01f), 1.0f));

    // Reduction is stored as a POSITIVE attenuation amount in dB (0 = no cut,
    // 60 = -60 dB cut). This is what makes the knob read left-to-right as
    // "more reduction": leftmost / fully down = 0, middle = 30 (-30 dB),
    // rightmost / fully up = 60 (-60 dB). processBlock negates the value
    // before handing it to the gate so the DSP still sees dB-below-unity.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("reduction", 1), "Reduction",
        juce::NormalisableRange<float>(0.0f, 60.0f, 0.1f), 30.0f));

    return { params.begin(), params.end() };
}

const juce::String TiptoeAudioProcessor::getName() const { return JucePlugin_Name; }
bool TiptoeAudioProcessor::acceptsMidi() const { return false; }
bool TiptoeAudioProcessor::producesMidi() const { return false; }
bool TiptoeAudioProcessor::isMidiEffect() const { return false; }
double TiptoeAudioProcessor::getTailLengthSeconds() const { return 0.0; }
int TiptoeAudioProcessor::getNumPrograms() { return 1; }
int TiptoeAudioProcessor::getCurrentProgram() { return 0; }
void TiptoeAudioProcessor::setCurrentProgram(int) {}
const juce::String TiptoeAudioProcessor::getProgramName(int) { return {}; }
void TiptoeAudioProcessor::changeProgramName(int, const juce::String&) {}

void TiptoeAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& g : gates)
        g.prepare(sampleRate, samplesPerBlock);

    // Report algorithmic latency so the DAW can delay-compensate other
    // tracks. With 512-sample FFT + 75 % overlap-add Hann analysis, the
    // input-to-output delay is kFFTSize samples (≈ 11.6 ms at 44.1 kHz).
    setLatencySamples(SpectralGateTiptoe::kFFTSize);
}

void TiptoeAudioProcessor::releaseResources()
{
    for (auto& g : gates)
        g.reset();
}

bool TiptoeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void TiptoeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    // Update parameters.
    // The "reduction" parameter is stored as positive attenuation dB (0 – 60)
    // so the UI reads left-to-right as "more reduction"; negate it here so
    // the DSP continues to see a signed gain value (0 dB = unity, -60 dB =
    // near-silence).
    float sensitivity      = apvts.getRawParameterValue("sensitivity")->load();
    float reductionAtten   = apvts.getRawParameterValue("reduction")->load();
    float reductionGainDb  = -reductionAtten;

    for (auto& g : gates)
    {
        g.setSensitivity(sensitivity);
        g.setReduction(reductionGainDb);
    }

    int numSamples = buffer.getNumSamples();

    // Feed audio for noise learning if active, and pass the input straight through
    // (no gating applied while learning) so the user hears the unaffected signal.
    if (learning_)
    {
        for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
            gates[ch].learnFromBlock(buffer.getReadPointer(ch), numSamples);
        return;
    }

    // Process each channel
    for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
        gates[ch].processMono(buffer.getWritePointer(ch), numSamples);
}

void TiptoeAudioProcessor::startLearning()
{
    learning_ = true;
    for (auto& g : gates)
        g.startLearning();
}

void TiptoeAudioProcessor::stopLearning()
{
    for (auto& g : gates)
        g.stopLearning();
    learning_ = false;
}

bool TiptoeAudioProcessor::isLearning() const
{
    return learning_;
}

float TiptoeAudioProcessor::getLastProcessingTimeMs() const
{
    return std::max(gates[0].getLastProcessingTimeMs(),
                    gates[1].getLastProcessingTimeMs());
}

juce::AudioProcessorEditor* TiptoeAudioProcessor::createEditor()
{
    return new TiptoeAudioProcessorEditor(*this);
}

bool TiptoeAudioProcessor::hasEditor() const { return true; }

void TiptoeAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void TiptoeAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(apvts.state.getType()))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TiptoeAudioProcessor();
}
