// Renders the STONE editor to a PNG after playing a few notes.
#include "StoneProcessor.h"
#include "StoneEditor.h"
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "stone.png"));
    StoneProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.setCurrentProgram (argc > 2 ? juce::String (argv[2]).getIntValue() : 0);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    juce::AudioBuffer<float> buf (2, 512);
    for (int b = 0; b < 120; ++b)
    {
        juce::MidiBuffer midi;
        if (b % 40 == 2) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + b / 10, 0.9f), 0);
        proc.processBlock (buf, midi);
        juce::Thread::sleep (2);
        juce::Timer::callPendingTimersSynchronously();
    }
    ed->setSize (StoneView::kWidth, StoneView::kHeight);
    juce::Timer::callPendingTimersSynchronously();
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, argc > 3 ? juce::String (argv[3]).getFloatValue() : 1.5f);
    juce::PNGImageFormat png; out.deleteFile(); juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    ed.reset();
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8());
    return ok ? 0 : 1;
}
