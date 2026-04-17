// Headless CLI that renders the plugin editor to a single PNG.
// Usage: TiptoeScreenshot <output.png> [width] [height] [scaleFactor]
// Default size is the plugin's preferred default; scaleFactor 2 renders
// at 2x for crisp retina display. The output file is always overwritten —
// we keep exactly one screenshot, not a history.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "KnobDesign.h"

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
    processor.prepareToPlay(44100.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor{ processor.createEditor() };
    if (editor == nullptr)
    {
        std::cerr << "error: createEditor() returned null\n";
        return 2;
    }
    editor->setSize(width, height);

    auto snap = editor->createComponentSnapshot(editor->getLocalBounds(), false, scale);

    // Crop off the bottom band equal to the conjius logo height so the footer
    // (latency label, etc.) doesn't appear in the published screenshot.
    const int cropPx = static_cast<int>(37.5f
                                        * (static_cast<float>(width) / static_cast<float>(KnobDesign::defaultWidth))
                                        * scale);
    if (cropPx > 0 && cropPx < snap.getHeight())
    {
        juce::Image cropped{ juce::Image::ARGB, snap.getWidth(), snap.getHeight() - cropPx, true };
        juce::Graphics cg{ cropped };
        cg.drawImageAt(snap, 0, 0); // anything past the cropped bounds is clipped off
        snap = cropped;
    }

    // Rounded corners + orange border around the edge.
    // Corner radius matches the knob radius at the default editor size
    // (knob diameter ≈ 0.78 × min(sliderW, knobAreaH) ≈ 156px at 2x render).
    {
        const float cornerRadius = 78.0f * scale;
        const float borderW = 4.0f * scale;
        const float w = static_cast<float>(snap.getWidth());
        const float h = static_cast<float>(snap.getHeight());

        juce::Image framed{ juce::Image::ARGB, snap.getWidth(), snap.getHeight(), true };
        juce::Graphics rg{ framed };

        // Clip to the rounded shape, then paint the editor snapshot
        juce::Path mask;
        mask.addRoundedRectangle(0.0f, 0.0f, w, h, cornerRadius);
        rg.reduceClipRegion(mask);
        rg.drawImageAt(snap, 0, 0);

        // Stroke the accent-colour border along the same rounded path — the
        // clip keeps the corners crisp and prevents the stroke bleeding out.
        rg.setColour(KnobDesign::accentColour);
        juce::Path border;
        border.addRoundedRectangle(borderW * 0.5f, borderW * 0.5f,
                                   w - borderW, h - borderW,
                                   cornerRadius - borderW * 0.5f);
        rg.strokePath(border, juce::PathStrokeType(borderW));

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
