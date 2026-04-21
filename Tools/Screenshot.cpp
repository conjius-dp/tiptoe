// Headless CLI that renders the plugin editor to a single PNG.
// Usage: TiptoeScreenshot <output.png> [width] [height] [scaleFactor]
// Default size is the plugin's preferred default; scaleFactor 2 renders
// at 2x for crisp retina display. The output file is always overwritten —
// we keep exactly one screenshot, not a history.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <random>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KnobDesign.h"

namespace
{
    // Paul Kellett's pink-noise generator. Stateful; emits samples at roughly
    // ±0.5 amplitude. Good enough shape for the README spectrum curves.
    struct PinkNoise
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
        std::mt19937 rng { 0x5eedu };
        std::normal_distribution<float> dist { 0.0f, 1.0f };

        float next()
        {
            const float white = dist(rng);
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f;
            b6 = white * 0.115926f;
            return out * 0.11f;
        }
    };

    void fillPinkStereo(juce::AudioBuffer<float>& buf, PinkNoise& pink, float gain)
    {
        for (int n = 0; n < buf.getNumSamples(); ++n)
        {
            const float s = pink.next() * gain;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample(ch, n, s);
        }
    }

    // Pink noise plus a handful of sine peaks, so the live-input curve has
    // visibly distinct bumps above the noise floor.
    void fillPinkPlusTones(juce::AudioBuffer<float>& buf,
                           PinkNoise& pink,
                           double sampleRate,
                           double& phase300,
                           double& phase1k,
                           double& phase3k)
    {
        const double twoPi = juce::MathConstants<double>::twoPi;
        const double inc300 = twoPi * 300.0  / sampleRate;
        const double inc1k  = twoPi * 1100.0 / sampleRate;
        const double inc3k  = twoPi * 4200.0 / sampleRate;

        for (int n = 0; n < buf.getNumSamples(); ++n)
        {
            const float tones = 0.35f * static_cast<float>(std::sin(phase300))
                              + 0.30f * static_cast<float>(std::sin(phase1k))
                              + 0.22f * static_cast<float>(std::sin(phase3k));
            phase300 += inc300;
            phase1k  += inc1k;
            phase3k  += inc3k;
            const float s = pink.next() * 0.6f + tones;
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                buf.setSample(ch, n, s);
        }
    }
}

static int parseInt(const char* s, int fallback)
{
    if (s == nullptr) return fallback;
    const auto str = juce::String(s).trim();
    return str.isEmpty() ? fallback : str.getIntValue();
}

static float parseFloat(const char* s, float fallback)
{
    if (s == nullptr) return fallback;
    const auto str = juce::String(s).trim();
    return str.isEmpty() ? fallback : static_cast<float>(str.getDoubleValue());
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: " << argv[0] << " <output.png> [width] [height] [scale]\n";
        return 1;
    }

    const juce::File outFile{ juce::String::fromUTF8(argv[1]) };
    const int   width  = parseInt (argc > 2 ? argv[2] : nullptr, KnobDesign::defaultWidth);
    const int   height = parseInt (argc > 3 ? argv[3] : nullptr, KnobDesign::defaultHeight);
    const float scale  = parseFloat(argc > 4 ? argv[4] : nullptr, 2.0f);

    juce::ScopedJuceInitialiser_GUI scopedGUI;

    TiptoeAudioProcessor processor;
    const double sampleRate = 44100.0;
    const int blockSize = 512;
    processor.prepareToPlay(sampleRate, blockSize);

    // ── Prime the spectrum graph with synthetic audio so the README image
    //    actually shows a noise-profile curve AND a live-input curve.
    {
        juce::AudioBuffer<float> buf(2, blockSize);
        juce::MidiBuffer midi;
        PinkNoise pink;

        // Learn a pink-noise "noise profile" — a few seconds at block size
        // gives the profile enough frames to look smooth.
        processor.startLearning();
        const int learnBlocks = static_cast<int>(sampleRate / blockSize) * 2; // ~2 s
        for (int i = 0; i < learnBlocks; ++i)
        {
            buf.clear();
            fillPinkStereo(buf, pink, 0.35f);
            processor.processBlock(buf, midi);
        }
        processor.stopLearning();

        // Now feed a louder pink-noise-plus-tones input so the live curve
        // sits visibly above the learned noise floor.
        double phase300 = 0.0, phase1k = 0.0, phase3k = 0.0;
        const int runBlocks = static_cast<int>(sampleRate / blockSize) / 2; // ~0.5 s
        for (int i = 0; i < runBlocks; ++i)
        {
            buf.clear();
            fillPinkPlusTones(buf, pink, sampleRate, phase300, phase1k, phase3k);
            processor.processBlock(buf, midi);
        }
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor{ processor.createEditor() };
    if (editor == nullptr)
    {
        std::cerr << "error: createEditor() returned null\n";
        return 2;
    }
    editor->setSize(width, height);

    // Hide the conjius logo + latency label so the README image is clean, and
    // pull the freshly-populated spectrum snapshot into the graph so it paints
    // before we snapshot (the editor timer hasn't run yet at this point).
    if (auto* tiptoeEditor = dynamic_cast<TiptoeAudioProcessorEditor*>(editor.get()))
    {
        tiptoeEditor->setChromeVisible(false);
        tiptoeEditor->refreshSpectrumGraph();
    }

    auto snap = editor->createComponentSnapshot(editor->getLocalBounds(), false, scale);

    // The editor already draws its own rounded orange border inset by `pad`
    // pixels from each edge. Crop the outer bg padding so the image's outer
    // edge aligns with that existing border, and mask the corners with the
    // same radius so they follow the border's curve. No additional border is
    // stroked — the editor's own border is the visible perimeter.
    {
        const float widthScale = static_cast<float>(width) / static_cast<float>(KnobDesign::defaultWidth);
        const int insetPx = static_cast<int>(20.0f * widthScale * scale);
        const float cornerRadius = 70.0f * widthScale * scale;

        const int outW = snap.getWidth() - 2 * insetPx;
        const int outH = snap.getHeight() - 2 * insetPx;

        juce::Image framed{ juce::Image::ARGB, outW, outH, true };
        juce::Graphics rg{ framed };

        juce::Path mask;
        mask.addRoundedRectangle(0.0f, 0.0f,
                                 static_cast<float>(outW),
                                 static_cast<float>(outH),
                                 cornerRadius);
        rg.reduceClipRegion(mask);
        rg.drawImageAt(snap, -insetPx, -insetPx);

        snap = framed;
    }

    outFile.deleteFile();
    juce::FileOutputStream stream{ outFile };
    if (!stream.openedOk())
    {
        std::cerr << "error: cannot write to " << outFile.getFullPathName() << "\n";
        return 3;
    }

    juce::PNGImageFormat png;
    if (!png.writeImageToStream(snap, stream))
    {
        std::cerr << "error: PNG encode failed\n";
        return 4;
    }

    std::cout << "wrote " << outFile.getFullPathName()
              << " (" << snap.getWidth() << "x" << snap.getHeight() << ")\n";
    return 0;
}
