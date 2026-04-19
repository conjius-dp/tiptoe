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

    // Bypass — exposed to the host via getBypassParameter() so DAWs that
    // drive native bypass through an automatable parameter stay in sync
    // with the in-plugin power button.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("bypass", 1), "Bypass", false));

    // HQ mode — false ⇒ multi-band realtime gate (3.67 ms), true ⇒
    // single-band high-resolution gate (11.6 ms). Exposed as a bool
    // parameter so hosts can automate / recall it like any other knob.
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("hq", 1), "HQ Mode", false));

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
    for (auto& g : gates)   g.prepare(sampleRate, samplesPerBlock);
    for (auto& g : hqGates) g.prepare(sampleRate, samplesPerBlock);

    // Report the CURRENT mode's latency. Mode toggles call
    // setLatencySamples() again; DAWs then re-compensate. Hosts handle
    // this differently — some smoothly adjust, some require a quick
    // replug — but the alternative (always reporting HQ's latency) would
    // force realtime mode to eat 8 ms of pointless delay.
    prevHQMode_ = isHQMode();
    setLatencySamples(prevHQMode_ ? SpectralGateTiptoe::kFFTSize
                                  : gates[0].getLatencyInSamples());
}

void TiptoeAudioProcessor::releaseResources()
{
    for (auto& g : gates)   g.reset();
    for (auto& g : hqGates) g.reset();
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

    // Hard bypass — input → output untouched, no gate, no learning, no
    // latency measurement. Returns before any parameter reads so the DSP
    // state is left exactly as it was.
    if (apvts.getRawParameterValue("bypass")->load() >= 0.5f)
        return;

    // Mode switch detection. Re-report latency when the user flips HQ.
    // JUCE asserts on cross-thread setLatencySamples on some hosts, but
    // processBlock runs on the audio thread where the host polls, so
    // it's safe here.
    const bool hq = isHQMode();
    if (hq != prevHQMode_)
    {
        setLatencySamples(hq ? SpectralGateTiptoe::kFFTSize
                             : gates[0].getLatencyInSamples());
        prevHQMode_ = hq;
    }

    // Parameter routing: "reduction" is a positive attenuation dB (0–60),
    // negated here so the DSP sees a signed gain (0 dB = unity, -60 dB
    // = near-silence).
    const float sensitivity      = apvts.getRawParameterValue("sensitivity")->load();
    const float reductionAtten   = apvts.getRawParameterValue("reduction")->load();
    const float reductionGainDb  = -reductionAtten;

    for (auto& g : gates)   { g.setSensitivity(sensitivity); g.setReduction(reductionGainDb); }
    for (auto& g : hqGates) { g.setSensitivity(sensitivity); g.setReduction(reductionGainDb); }

    const int numSamples = buffer.getNumSamples();

    // Learning feeds BOTH gates so the user can toggle modes after
    // learning without losing the profile on the other side.
    if (learning_)
    {
        for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
        {
            const float* read = buffer.getReadPointer(ch);
            gates  [ch].learnFromBlock(read, numSamples);
            hqGates[ch].learnFromBlock(read, numSamples);
        }
        return;
    }

    // Snapshot channel-0 input BEFORE processing — used in realtime
    // mode to feed the HQ gate's FFT for visualization. Must be captured
    // before the active gate writes processed output back to the buffer.
    static thread_local std::vector<float> vizScratch;
    if (! hq)
    {
        const float* in0 = buffer.getReadPointer(0);
        vizScratch.assign(in0, in0 + numSamples);
    }

    // Route processing based on the active mode.
    for (int ch = 0; ch < std::min(totalNumInputChannels, 2); ++ch)
    {
        if (hq) hqGates[ch].processMono(buffer.getWritePointer(ch), numSamples);
        else    gates  [ch].processMono(buffer.getWritePointer(ch), numSamples);
    }

    // In realtime mode, feed the HQ gate the SAVED raw input (channel 0)
    // and discard its output. The spectrum graph always reads HQ's
    // published FFT snapshot, so realtime and HQ visualizations stay at
    // identical resolution (FFT 512, 257 bins) and scale.
    if (! hq)
        hqGates[0].processMono(vizScratch.data(), numSamples);
}

void TiptoeAudioProcessor::startLearning()
{
    learning_ = true;
    for (auto& g : gates)   g.startLearning();
    for (auto& g : hqGates) g.startLearning();
}

void TiptoeAudioProcessor::stopLearning()
{
    for (auto& g : gates)   g.stopLearning();
    for (auto& g : hqGates) g.stopLearning();
    learning_ = false;
}

bool TiptoeAudioProcessor::isLearning() const
{
    return learning_;
}

float TiptoeAudioProcessor::getLastProcessingTimeMs() const
{
    // MultiBandGate doesn't track wall-clock — the editor displays
    // algorithmic latency anyway. Return 0 for the CPU-time diagnostic.
    return 0.0f;
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
