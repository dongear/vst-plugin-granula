#include "GardenProcessor.h"
#include "GardenEditor.h"

using namespace garden;
using APF = juce::AudioParameterFloat;
using API = juce::AudioParameterInt;
using APC = juce::AudioParameterChoice;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
juce::ParameterID pid (const char* id) { return { id, 1 }; }
Attr pct() { return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }); }
Attr bip() { return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; }); }
Attr secs()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; });
}
juce::NormalisableRange<float> skew (float a, float b, float c) { juce::NormalisableRange<float> r (a, b); r.setSkewForCentre (c); return r; }

const int touchMaterialMap[] = { stone::Wood, stone::Glass, stone::Skin, stone::Water, stone::Stone, stone::Ceramic, stone::Crystal, stone::Metal };

juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<API> (pid (ids::seed), "Seed", 1, 9999, 1));
    l.add (std::make_unique<APF> (pid (ids::growth), "Growth", juce::NormalisableRange<float> (0, 1), 0.35f, pct()));
    l.add (std::make_unique<APF> (pid (ids::branch), "Branch", juce::NormalisableRange<float> (0, 1), 0.35f, pct()));
    l.add (std::make_unique<APF> (pid (ids::bend), "Bend", juce::NormalisableRange<float> (-1, 1), 0.0f, bip()));
    l.add (std::make_unique<APF> (pid (ids::leaves), "Leaves", juce::NormalisableRange<float> (0, 1), 0.4f, pct()));
    l.add (std::make_unique<APF> (pid (ids::roots), "Roots", juce::NormalisableRange<float> (0, 1), 0.35f, pct()));
    l.add (std::make_unique<APF> (pid (ids::sun), "Sun", juce::NormalisableRange<float> (0, 1), 0.55f, pct()));
    l.add (std::make_unique<APF> (pid (ids::width), "Width", juce::NormalisableRange<float> (0, 1), 0.6f, pct()));
    l.add (std::make_unique<APF> (pid (ids::water), "Water", juce::NormalisableRange<float> (0, 1), 0.15f, pct()));
    l.add (std::make_unique<APF> (pid (ids::wind), "Wind", juce::NormalisableRange<float> (0, 1), 0.35f, pct()));
    l.add (std::make_unique<APF> (pid (ids::season), "Season", juce::NormalisableRange<float> (0, 1), 0.3f,
                                  Attr().withStringFromValueFunction ([] (float v, int)
                                  {
                                      return v < 0.25f ? juce::String ("Spring") : v < 0.5f ? juce::String ("Summer") : v < 0.75f ? juce::String ("Autumn") : juce::String ("Winter");
                                  })));
    l.add (std::make_unique<APF> (pid (ids::bloom), "Bloom", skew (0.002f, 6.0f, 0.3f), 0.06f, secs()));
    l.add (std::make_unique<APF> (pid (ids::wither), "Wither", skew (0.02f, 10.0f, 1.0f), 1.0f, secs()));
    l.add (std::make_unique<APF> (pid (ids::touch), "Touch", juce::NormalisableRange<float> (0, 1), 0.25f, pct()));
    l.add (std::make_unique<APC> (pid (ids::touchMat), "Touch Material",
                                  juce::StringArray { "Wood", "Glass", "Skin", "Water", "Stone", "Ceramic", "Crystal", "Metal" }, 0));
    l.add (std::make_unique<APC> (pid (ids::octave), "Octave", juce::StringArray { "-2", "-1", "0", "+1", "+2" }, 2));
    l.add (std::make_unique<APF> (pid (ids::chorus), "Chorus", juce::NormalisableRange<float> (0, 1), 0.25f, pct()));
    l.add (std::make_unique<APF> (pid (ids::space), "Space", juce::NormalisableRange<float> (0, 1), 0.3f, pct()));
    l.add (std::make_unique<APF> (pid (ids::spaceSize), "Space Size", juce::NormalisableRange<float> (0, 1), 0.7f, pct()));
    l.add (std::make_unique<APF> (pid (ids::warmth), "Warmth", juce::NormalisableRange<float> (0, 1), 0.2f, pct()));
    l.add (std::make_unique<APF> (pid (ids::out), "Output", juce::NormalisableRange<float> (-24, 12, 0.1f), 0.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

std::vector<common::FactoryPreset> factory()
{
    namespace I = ids;
    return {
        { "Sprout", {} },
        { "Glass Petals", { { I::seed, 21 }, { I::growth, 0.55f }, { I::branch, 0.3f }, { I::bend, 0.35f }, { I::leaves, 0.8f }, { I::roots, 0.2f },
                            { I::sun, 0.7f }, { I::bloom, 0.25f }, { I::wither, 1.8f }, { I::season, 0.2f }, { I::water, 0.15f }, { I::wind, 0.6f },
                            { I::touch, 0.4f }, { I::touchMat, 1 }, { I::chorus, 0.3f }, { I::space, 0.35f } } },
        { "Autumn Breath Pad", { { I::seed, 3 }, { I::growth, 0.9f }, { I::branch, 0.8f }, { I::bend, -0.3f }, { I::leaves, 0.2f }, { I::roots, 0.8f },
                                 { I::sun, 0.3f }, { I::bloom, 1.5f }, { I::wither, 4.0f }, { I::season, 0.8f }, { I::water, 0.6f }, { I::wind, 0.4f },
                                 { I::touch, 0.0f }, { I::space, 0.45f }, { I::chorus, 0.4f } } },
        { "Dew Pluck", { { I::seed, 5 }, { I::growth, 0.15f }, { I::roots, 0.3f }, { I::bloom, 0.005f }, { I::wither, 0.6f }, { I::touch, 0.6f },
                         { I::touchMat, 3 }, { I::wind, 0.2f }, { I::season, 0.5f }, { I::space, 0.25f } } },
        { "Moss Keys", { { I::seed, 88 }, { I::growth, 0.45f }, { I::branch, 0.6f }, { I::bend, 0.1f }, { I::sun, 0.45f }, { I::leaves, 0.3f },
                         { I::bloom, 0.01f }, { I::wither, 1.2f }, { I::touch, 0.5f }, { I::touchMat, 0 }, { I::warmth, 0.4f } } },
        { "Fern Choir", { { I::seed, 404 }, { I::growth, 0.8f }, { I::branch, 1.0f }, { I::water, 0.45f }, { I::wind, 0.7f }, { I::bloom, 0.9f },
                          { I::wither, 2.5f }, { I::chorus, 0.6f }, { I::space, 0.5f }, { I::sun, 0.55f }, { I::touch, 0.0f } } },
        { "Crystal Garden", { { I::seed, 777 }, { I::growth, 0.7f }, { I::bend, 0.8f }, { I::leaves, 1.0f }, { I::sun, 0.85f }, { I::touch, 0.5f },
                              { I::touchMat, 6 }, { I::bloom, 0.02f }, { I::wither, 3.0f }, { I::space, 0.5f } } },
        { "Root Bass", { { I::seed, 12 }, { I::growth, 0.3f }, { I::roots, 1.0f }, { I::sun, 0.25f }, { I::octave, 1 }, { I::bloom, 0.01f },
                         { I::wither, 0.4f }, { I::touch, 0.3f }, { I::touchMat, 2 }, { I::warmth, 0.5f }, { I::chorus, 0.0f }, { I::space, 0.1f } } },
        { "Cloud Bloom", { { I::seed, 1999 }, { I::growth, 1.0f }, { I::branch, 0.5f }, { I::bend, 0.15f }, { I::leaves, 0.7f }, { I::water, 0.35f },
                           { I::wind, 0.8f }, { I::bloom, 2.5f }, { I::wither, 5.0f }, { I::chorus, 0.7f }, { I::space, 0.6f }, { I::season, 0.1f }, { I::touch, 0.0f } } },
        { "Rain Bells", { { I::seed, 63 }, { I::growth, 0.5f }, { I::bend, 0.6f }, { I::leaves, 0.6f }, { I::touch, 0.7f }, { I::touchMat, 3 },
                          { I::bloom, 0.003f }, { I::wither, 2.0f }, { I::space, 0.4f }, { I::wind, 0.3f } } },
        { "Wilted Organ", { { I::seed, 250 }, { I::growth, 0.6f }, { I::branch, 0.2f }, { I::bend, -0.6f }, { I::season, 1.0f }, { I::sun, 0.35f },
                            { I::bloom, 0.3f }, { I::wither, 1.5f }, { I::warmth, 0.6f }, { I::touch, 0.0f } } },
        { "Sunflower Lead", { { I::seed, 31 }, { I::growth, 0.65f }, { I::sun, 0.95f }, { I::leaves, 0.9f }, { I::branch, 0.4f }, { I::wind, 0.35f },
                              { I::bloom, 0.04f }, { I::wither, 0.9f }, { I::chorus, 0.25f }, { I::space, 0.3f } } },
    };
}
} // namespace

GardenProcessor::GardenProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GardenState", layout()),
      presets (*this, apvts, "Garden Presets", ".garden", factory())
{
}

void GardenProcessor::prepareToPlay (double sr, int)
{
    engine.prepare (sr);
    chorus.prepare (sr);
    reverb.prepare (sr);
    tape.prepare (sr);
    outSm.prepare (sr, 20.0f); outSm.snap (granula::dbToGain (get (ids::out)));
    setLatencySamples (tape.getLatencySamples());
    plantValid = false;
}

PlantParams GardenProcessor::plantParams() const
{
    PlantParams p;
    p.seed = (int) get (ids::seed);
    p.growth = get (ids::growth);
    p.branch = get (ids::branch);
    p.bend = get (ids::bend);
    p.leaves = get (ids::leaves);
    p.roots = get (ids::roots);
    p.sun = get (ids::sun);
    p.width = get (ids::width);
    return p;
}

void GardenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    // rebuild the plant only when its shape changed
    const auto pp = plantParams();
    if (! plantValid || std::memcmp (&pp, &lastPlant, sizeof (PlantParams)) != 0)
    {
        engine.setPlant (buildPlant (pp));
        lastPlant = pp;
        plantValid = true;
    }

    if (const int pe = previewEvent.exchange (0)) { if (pe == 1) engine.noteOn (60, 0.8f); else engine.noteOff (60); }

    VoiceParams vp;
    vp.bloomSec = get (ids::bloom);
    vp.witherSec = get (ids::wither);
    vp.season = get (ids::season);
    vp.water = get (ids::water);
    vp.touch = get (ids::touch);
    vp.touchMaterial = touchMaterialMap[juce::jlimit (0, 7, (int) get (ids::touchMat))];
    vp.octave = 12.0f * (float) ((int) get (ids::octave) - 2);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // split the block at MIDI events for tight timing
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        vp.wind = std::clamp (get (ids::wind) + modWheel, 0.0f, 1.0f);
        vp.bendSemis = bendSemis;
        if (end > pos) engine.render (L + pos, R + pos, end - pos, vp);
        pos = end;
    };
    for (const auto meta : midi)
    {
        renderTo (juce::jlimit (pos, n, meta.samplePosition));
        const auto m = meta.getMessage();
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isSustainPedalOn()) engine.setSustain (true);
        else if (m.isSustainPedalOff()) engine.setSustain (false);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allOff();
        else if (m.isPitchWheel()) bendSemis = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        else if (m.isControllerOfType (1)) modWheel = (float) m.getControllerValue() / 127.0f * 0.6f;
    }
    renderTo (n);
    midi.clear();

    chorus.process (L, R, n, get (ids::chorus));

    granula::TapeSaturation::Params tp;
    const float w = get (ids::warmth);
    tp.enabled = true;
    tp.driveDb = w * 14.0f;
    tp.wow = w * 0.15f;
    tp.flutter = w * 0.05f;
    tp.age = w * 0.3f;
    tp.hiss = 0.0f;
    tape.process (L, R, n, tp);

    granula::FdnReverb::Params rp;
    rp.enabled = true;
    rp.mix = get (ids::space);
    rp.size = get (ids::spaceSize);
    rp.decaySec = 1.0f + get (ids::spaceSize) * 7.0f;
    rp.damping = 0.4f;
    rp.preDelayMs = 18.0f;
    reverb.process (L, R, n, rp);

    outSm.target = granula::dbToGain (get (ids::out));
    for (int i = 0; i < n; ++i)
    {
        const float g = outSm.next();
        auto safe = [] (float x)
        {
            if (! std::isfinite (x)) return 0.0f;
            const float a = std::abs (x);
            return a <= 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) / 0.1f), x);
        };
        L[i] = safe (L[i] * g);
        R[i] = safe (R[i] * g);
    }
}

void GardenProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    presets.writeState (st);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void GardenProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml);
            presets.readState (st);
            apvts.replaceState (st);
        }
}

juce::AudioProcessorEditor* GardenProcessor::createEditor() { return new GardenEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GardenProcessor(); }
