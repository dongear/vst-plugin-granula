// Renders the Interference editor to a PNG after feeding it some audio.
// Usage: Interference_snapshot <out.png> [preset] [scale]
#include "InterferenceProcessor.h"
#include "InterferenceEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "interference.png"));
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 0;
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 1.5f;

    InterferenceProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.setCurrentProgram (preset);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());

    // dissonant pad: C major-ish chord plus out-of-key partials and noise
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    granula::Rng rng (3);
    const double fs[] = { 130.81, 196.0, 261.63, 311.13, 329.63, 369.99, 523.25, 587.33, 739.99, 1046.5, 1244.5 };
    for (int b = 0; b < 300; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double t = (b * 512 + i) / 48000.0;
            double v = 0.02 * rng.bi();
            for (double f : fs) v += 0.05 * std::sin (2.0 * juce::MathConstants<double>::pi * f * t);
            buf.setSample (0, i, (float) v); buf.setSample (1, i, (float) v);
        }
        proc.processBlock (buf, midi);
        if (b % 8 == 0) { juce::Thread::sleep (2); juce::Timer::callPendingTimersSynchronously(); }
    }
    ed->setSize (InterferenceView::kWidth, InterferenceView::kHeight);
    for (int i = 0; i < 6; ++i) { juce::Thread::sleep (40); juce::Timer::callPendingTimersSynchronously(); }
    auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
    juce::PNGImageFormat png;
    out.deleteFile();
    juce::FileOutputStream os (out);
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    ed.reset();
    std::printf ("%s %s  latency=%d\n", ok ? "wrote" : "FAILED", out.getFullPathName().toRawUTF8(), proc.getLatencySamples());
    return ok ? 0 : 1;
}
