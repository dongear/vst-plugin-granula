#include "Parameters.h"

namespace granula
{
namespace
{
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Attr = juce::AudioParameterFloatAttributes;

juce::ParameterID pid (const char* id) { return { id, 1 }; }

juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

Attr msAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms";
    }).withLabel ("ms");
}

Attr pctAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; });
}

Attr hzAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (v, v < 10.0f ? 1 : 0) + " Hz";
    });
}

Attr stAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st";
    });
}

Attr dbAttr()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
    });
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ---- Grain ----
    auto grain = std::make_unique<juce::AudioProcessorParameterGroup> ("grain", "Grain", "|");
    grain->addChild (std::make_unique<APF> (pid (ids::size), "Grain Size", skewed (5.0f, 1000.0f, 120.0f), 120.0f, msAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::density), "Density", skewed (0.5f, 100.0f, 12.0f), 12.0f, hzAttr()));
    grain->addChild (std::make_unique<APB> (pid (ids::sync), "Density Sync", false));
    grain->addChild (std::make_unique<APC> (pid (ids::division), "Density Division", divisionNames(), 4));
    grain->addChild (std::make_unique<APF> (pid (ids::position), "Position", skewed (0.0f, 2000.0f, 300.0f), 80.0f, msAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::spray), "Spray", skewed (0.0f, 1000.0f, 150.0f), 40.0f, msAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::pitch), "Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, stAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::pitchRand), "Pitch Random", juce::NormalisableRange<float> (0.0f, 24.0f, 0.01f), 0.0f, stAttr()));
    grain->addChild (std::make_unique<APC> (pid (ids::scale), "Scale", scaleNames(), 0));
    grain->addChild (std::make_unique<APF> (pid (ids::detune), "Detune", juce::NormalisableRange<float> (0.0f, 50.0f, 0.1f), 6.0f,
                                            Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " ct"; })));
    grain->addChild (std::make_unique<APF> (pid (ids::reverse), "Reverse", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::spread), "Spread", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f, pctAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::shape), "Shape", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pctAttr()));
    grain->addChild (std::make_unique<APF> (pid (ids::feedback), "Grain Feedback", juce::NormalisableRange<float> (0.0f, 0.95f), 0.0f, pctAttr()));
    grain->addChild (std::make_unique<APB> (pid (ids::freeze), "Freeze", false));
    grain->addChild (std::make_unique<APF> (pid (ids::grainMix), "Grain Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f, pctAttr()));
    layout.add (std::move (grain));

    // ---- Tape ----
    auto tape = std::make_unique<juce::AudioProcessorParameterGroup> ("tape", "Tape", "|");
    tape->addChild (std::make_unique<APB> (pid (ids::tapeOn), "Tape On", true));
    tape->addChild (std::make_unique<APF> (pid (ids::tapeDrive), "Tape Drive", juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 6.0f, dbAttr()));
    tape->addChild (std::make_unique<APF> (pid (ids::tapeWow), "Wow", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, pctAttr()));
    tape->addChild (std::make_unique<APF> (pid (ids::tapeFlutter), "Flutter", juce::NormalisableRange<float> (0.0f, 1.0f), 0.15f, pctAttr()));
    tape->addChild (std::make_unique<APF> (pid (ids::tapeAge), "Age", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, pctAttr()));
    tape->addChild (std::make_unique<APF> (pid (ids::tapeHiss), "Hiss", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pctAttr()));
    layout.add (std::move (tape));

    // ---- Delay ----
    auto delay = std::make_unique<juce::AudioProcessorParameterGroup> ("delay", "Delay", "|");
    delay->addChild (std::make_unique<APB> (pid (ids::delayOn), "Delay On", true));
    delay->addChild (std::make_unique<APF> (pid (ids::delayTime), "Delay Time", skewed (1.0f, 2000.0f, 350.0f), 375.0f, msAttr()));
    delay->addChild (std::make_unique<APB> (pid (ids::delaySync), "Delay Sync", true));
    delay->addChild (std::make_unique<APC> (pid (ids::delayDiv), "Delay Division", divisionNames(), 10));
    delay->addChild (std::make_unique<APF> (pid (ids::delayFb), "Delay Feedback", juce::NormalisableRange<float> (0.0f, 0.95f), 0.4f, pctAttr()));
    delay->addChild (std::make_unique<APF> (pid (ids::delayTone), "Delay Tone", skewed (300.0f, 20000.0f, 3000.0f), 5000.0f, hzAttr()));
    delay->addChild (std::make_unique<APB> (pid (ids::delayPing), "Ping-Pong", true));
    delay->addChild (std::make_unique<APF> (pid (ids::delayMix), "Delay Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.25f, pctAttr()));
    layout.add (std::move (delay));

    // ---- Reverb ----
    auto rev = std::make_unique<juce::AudioProcessorParameterGroup> ("reverb", "Reverb", "|");
    rev->addChild (std::make_unique<APB> (pid (ids::revOn), "Reverb On", true));
    rev->addChild (std::make_unique<APF> (pid (ids::revSize), "Reverb Size", juce::NormalisableRange<float> (0.0f, 1.0f), 0.6f, pctAttr()));
    rev->addChild (std::make_unique<APF> (pid (ids::revDecay), "Decay", skewed (0.2f, 20.0f, 3.0f), 3.5f,
                                          Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; })));
    rev->addChild (std::make_unique<APF> (pid (ids::revDamp), "Damping", juce::NormalisableRange<float> (0.0f, 1.0f), 0.45f, pctAttr()));
    rev->addChild (std::make_unique<APF> (pid (ids::revPre), "Pre-Delay", juce::NormalisableRange<float> (0.0f, 250.0f, 0.1f), 20.0f, msAttr()));
    rev->addChild (std::make_unique<APF> (pid (ids::revWidth), "Width", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttr()));
    rev->addChild (std::make_unique<APF> (pid (ids::revMix), "Reverb Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pctAttr()));
    layout.add (std::move (rev));

    // ---- Output ----
    layout.add (std::make_unique<APF> (pid (ids::outGain), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, dbAttr()));

    return layout;
}
} // namespace granula
