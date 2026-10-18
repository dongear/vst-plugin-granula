// Renders the plugin editor to a PNG without a host (for previews / docs).
// Usage: <target>_snapshot <out.png> [presetIndex] [scale] [sampleFile]
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "granula.png"));
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 2.0f;
    const juce::String samplePath = argc > 4 ? juce::String (argv[4]) : juce::String();

    auto proc = std::make_unique<GranulaProcessor>();
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->setCurrentProgram (preset);

    if (samplePath.isNotEmpty())
    {
        proc->loadSampleAsync (juce::File::getCurrentWorkingDirectory().getChildFile (samplePath));
        for (int i = 0; i < 500 && (proc->isLoadingSample() || proc->getSampleVersion() == 0); ++i)
            juce::Thread::sleep (10);
        std::printf ("sample: %s\n", proc->getSample() ? proc->getSample()->name.c_str() : proc->getSampleError().toRawUTF8());
    }

    // Feed some audio / notes so the grain display has something to draw.
    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 200; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 10) { midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 67, 0.7f), 0); }
        for (int i = 0; i < 512; ++i)
        {
            const float t = (float) (b * 512 + i) / 48000.0f;
            const float v = 0.3f * std::sin (6.2831853f * 220.0f * t) * std::exp (-std::fmod (t, 0.375f) * 6.0f);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        proc->processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    ed->setSize (MainView::kWidth, MainView::kHeight);
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);

    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    ed.reset();
    proc.reset();
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
