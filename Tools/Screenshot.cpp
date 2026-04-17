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
