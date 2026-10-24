#include "StoneProcessor.h"
#include "StoneEditor.h"
#include "../../Source/Parameters.h"

using namespace stone;
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
juce::ParameterID pid (const std::string& id) { return { id, 1 }; }
Attr pct() { return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }); }
Attr bip() { return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%"; }); }
Attr st() { return Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.005f ? "+" : "") + juce::String (v, 1) + " st"; }); }
juce::NormalisableRange<float> skew (float a, float b, float c) { juce::NormalisableRange<float> r (a, b); r.setSkewForCentre (c); return r; }

juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    juce::StringArray mats, excs, srcs, dsts;
    for (int i = 0; i < NumMaterials; ++i) mats.add (material (i).name);
    for (int i = 0; i < NumExciters; ++i) excs.add (exciterName (i));
    for (int i = 0; i < NumSources; ++i) srcs.add (sourceName (i));
    for (int i = 0; i < NumDests; ++i) dsts.add (destName (i));

    l.add (std::make_unique<APC> (pid (ids::material), "Material", mats, 0));
    l.add (std::make_unique<APC> (pid (ids::exciter), "Exciter", excs, 1));
    l.add (std::make_unique<APF> (pid (ids::hardness), "Hardness", juce::NormalisableRange<float> (0, 1), 0.6f, pct()));
    l.add (std::make_unique<APF> (pid (ids::rough), "Roughness", juce::NormalisableRange<float> (0, 1), 0.15f, pct()));
    l.add (std::make_unique<APF> (pid (ids::decay), "Resonance", juce::NormalisableRange<float> (0, 1), 0.5f, pct()));
    l.add (std::make_unique<APF> (pid (ids::bright), "Brightness", juce::NormalisableRange<float> (0, 1), 0.6f, pct()));
    l.add (std::make_unique<APF> (pid (ids::damp), "Damping", juce::NormalisableRange<float> (0, 1), 0.5f, pct()));
    l.add (std::make_unique<APF> (pid (ids::pitch), "Pitch", juce::NormalisableRange<float> (-24, 24, 0.01f), 0.0f, st()));
    l.add (std::make_unique<APF> (pid (ids::inharm), "Inharmonic", juce::NormalisableRange<float> (-1, 1), 0.0f, bip()));
    l.add (std::make_unique<APF> (pid (ids::click), "Click", juce::NormalisableRange<float> (0, 1), 0.15f, pct()));

    l.add (std::make_unique<API> (pid (ids::hits), "Hits", 1, 32, 1));
    l.add (std::make_unique<APF> (pid (ids::rate), "Rate", skew (0.5f, 40.0f, 8.0f), 10.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " Hz"; })));
    l.add (std::make_unique<APB> (pid (ids::sync), "Sync", false));
    l.add (std::make_unique<APC> (pid (ids::div), "Division", granula::divisionNames(), 4));
    l.add (std::make_unique<APF> (pid (ids::accel), "Accelerate", juce::NormalisableRange<float> (-1, 1), 0.0f, bip()));
    l.add (std::make_unique<APF> (pid (ids::jitter), "Jitter", juce::NormalisableRange<float> (0, 1), 0.2f, pct()));
    l.add (std::make_unique<APF> (pid (ids::density), "Density", juce::NormalisableRange<float> (0, 1), 1.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::hitDecay), "Hit Decay", juce::NormalisableRange<float> (0.3f, 1.0f), 0.8f, pct()));
    l.add (std::make_unique<APF> (pid (ids::drift), "Drift", juce::NormalisableRange<float> (-2, 2, 0.01f), 0.0f, st()));
    l.add (std::make_unique<APF> (pid (ids::spread), "Spread", juce::NormalisableRange<float> (0, 1), 0.5f, pct()));
    l.add (std::make_unique<APB> (pid (ids::hold), "Hold Roll", false));

    for (int i = 0; i < 4; ++i)
    {
        l.add (std::make_unique<APC> (pid (ids::modSrc (i)), "Mod " + juce::String (i + 1) + " Source", srcs, 0));
        l.add (std::make_unique<APC> (pid (ids::modDst (i)), "Mod " + juce::String (i + 1) + " Dest", dsts, i));
        l.add (std::make_unique<APF> (pid (ids::modAmt (i)), "Mod " + juce::String (i + 1) + " Amount", juce::NormalisableRange<float> (-1, 1), 0.0f, bip()));
    }
    l.add (std::make_unique<APF> (pid (ids::lfoRate), "LFO Rate", skew (0.05f, 20.0f, 2.0f), 1.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; })));
    l.add (std::make_unique<APC> (pid (ids::lfoShape), "LFO Shape", juce::StringArray { "Sine", "Triangle", "Square", "Random" }, 0));
    for (int i = 0; i < 8; ++i)
        l.add (std::make_unique<APF> (pid (ids::step (i)), "Step " + juce::String (i + 1), juce::NormalisableRange<float> (0, 1), 0.5f, pct()));

    l.add (std::make_unique<APF> (pid (ids::grit), "Grit", juce::NormalisableRange<float> (0, 1), 0.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::verbMix), "Space", juce::NormalisableRange<float> (0, 1), 0.2f, pct()));
    l.add (std::make_unique<APF> (pid (ids::verbSize), "Space Size", juce::NormalisableRange<float> (0, 1), 0.5f, pct()));
    l.add (std::make_unique<APF> (pid (ids::verbDecay), "Space Decay", skew (0.3f, 12.0f, 2.5f), 2.5f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " s"; })));
    l.add (std::make_unique<APF> (pid (ids::out), "Output", juce::NormalisableRange<float> (-24, 12, 0.1f), 0.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

std::vector<common::FactoryPreset> factory()
{
    namespace I = ids;
    using I::modSrc; using I::modDst; using I::modAmt; using I::step;
    return {
        { "Init", {} },
        { "Pebble Roll", { { I::material, Stone }, { I::exciter, Noise }, { I::hardness, 0.45f }, { I::hits, 16 }, { I::rate, 18 }, { I::accel, -0.4f },
                           { I::jitter, 0.5f }, { I::decay, 0.3f }, { I::spread, 0.6f }, { I::rough, 0.5f }, { I::hitDecay, 0.9f } } },
        { "Glass Rain", { { I::material, Glass }, { I::hardness, 0.8f }, { I::hits, 12 }, { I::rate, 9 }, { I::accel, 0.5f }, { I::jitter, 0.35f },
                          { I::hitDecay, 0.82f }, { I::drift, 0.2f }, { I::spread, 0.8f }, { I::verbMix, 0.35f },
                          { modSrc (0), SrcRandom }, { modDst (0), DstPitch }, { modAmt (0), 0.08f } } },
        { "Wood Knock", { { I::material, Wood }, { I::decay, 0.4f }, { I::click, 0.3f }, { I::hardness, 0.7f }, { I::verbMix, 0.12f } } },
        { "Water Drops", { { I::material, Water }, { I::exciter, Click }, { I::hits, 6 }, { I::rate, 7 }, { I::jitter, 0.8f }, { I::spread, 1.0f },
                           { I::verbMix, 0.3f }, { modSrc (0), SrcRandom }, { modDst (0), DstPitch }, { modAmt (0), 0.35f } } },
        { "Crystal Bells", { { I::material, Crystal }, { I::hardness, 0.9f }, { I::decay, 0.8f }, { I::bright, 0.8f }, { I::verbMix, 0.45f }, { I::verbDecay, 5.0f } } },
        { "Metal Shimmer Roll", { { I::material, Metal }, { I::exciter, Noise }, { I::hold, 1 }, { I::rate, 24 }, { I::decay, 0.7f }, { I::spread, 1.0f },
                                  { I::hitDecay, 1.0f }, { modSrc (0), SrcLfo }, { modDst (0), DstPan }, { modAmt (0), 1.0f }, { I::lfoRate, 0.7f } } },
        { "Skin Tom", { { I::material, Skin }, { I::hardness, 0.5f }, { I::decay, 0.45f }, { I::pitch, -12.0f }, { I::click, 0.25f } } },
        { "Ceramic Ratchet", { { I::material, Ceramic }, { I::exciter, Click }, { I::hits, 24 }, { I::rate, 30 }, { I::accel, 0.8f }, { I::hitDecay, 0.9f },
                               { I::spread, 0.4f } } },
        { "Stone Steps (Sync)", { { I::material, Stone }, { I::sync, 1 }, { I::div, 4 }, { I::hits, 8 }, { I::hitDecay, 1.0f },
                                  { modSrc (0), SrcStep }, { modDst (0), DstLevel }, { modAmt (0), 0.8f },
                                  { modSrc (1), SrcStep }, { modDst (1), DstPitch }, { modAmt (1), 0.25f },
                                  { step (0), 1.0f }, { step (1), 0.2f }, { step (2), 0.6f }, { step (3), 0.1f },
                                  { step (4), 0.9f }, { step (5), 0.3f }, { step (6), 0.7f }, { step (7), 0.0f } } },
        { "Gravel (Hold)", { { I::material, Stone }, { I::exciter, Scrape }, { I::hold, 1 }, { I::rate, 30 }, { I::jitter, 1.0f }, { I::rough, 1.0f },
                             { I::density, 0.6f }, { I::grit, 0.4f }, { I::hitDecay, 1.0f }, { I::spread, 0.9f } } },
        { "Glass Spiral", { { I::material, Glass }, { I::exciter, Click }, { I::hits, 16 }, { I::drift, 1.0f }, { I::accel, 0.3f }, { I::rate, 12 },
                            { I::verbMix, 0.4f }, { I::spread, 1.0f }, { modSrc (0), SrcTrail }, { modDst (0), DstPan }, { modAmt (0), 1.0f } } },
        { "Bubble Field", { { I::material, Water }, { I::exciter, Click }, { I::hold, 1 }, { I::rate, 14 }, { I::jitter, 1.0f }, { I::density, 0.55f },
                            { I::spread, 1.0f }, { I::verbMix, 0.35f }, { modSrc (0), SrcRandom }, { modDst (0), DstPitch }, { modAmt (0), 0.6f } } },
        { "Moss Hits", { { I::material, Wood }, { I::exciter, Noise }, { I::hardness, 0.3f }, { I::decay, 0.35f }, { I::bright, 0.35f }, { I::hits, 3 },
                         { I::rate, 16 }, { I::jitter, 0.6f }, { I::hitDecay, 0.6f }, { I::grit, 0.2f }, { I::verbMix, 0.25f } } },
    };
}
} // namespace

StoneProcessor::StoneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "StoneState", layout()),
      presets (*this, apvts, "Stone Presets", ".stone", factory())
{
}

void StoneProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    engine.prepare (sr);
    reverb.prepare (sr);
    outSm.prepare (sr, 20.0f); outSm.snap (granula::dbToGain (get (ids::out)));
    gritSm.prepare (sr, 20.0f); gritSm.snap (get (ids::grit));
}

EngineParams StoneProcessor::currentParams() const
{
    EngineParams p;
    p.body.material = (int) get (ids::material);
    p.body.exciter = (int) get (ids::exciter);
    p.body.hardness = get (ids::hardness);
    p.body.rough = get (ids::rough);
    p.body.decay = get (ids::decay);
    p.body.bright = get (ids::bright);
    p.body.damp = get (ids::damp);
    p.body.inharm = get (ids::inharm);
    p.body.click = get (ids::click);
    p.pitchSemis = get (ids::pitch);
    p.hits = (int) std::lround (get (ids::hits));
    p.intervalSec = get (ids::sync) > 0.5f ? 60.0 / bpm * granula::divisionBeats ((int) get (ids::div)) : 1.0 / std::max (0.1f, get (ids::rate));
    p.accel = get (ids::accel);
    p.jitter = get (ids::jitter);
    p.density = get (ids::density);
    p.hitDecay = get (ids::hitDecay);
    p.drift = get (ids::drift);
    p.spread = get (ids::spread);
    p.hold = get (ids::hold) > 0.5f;
    for (int i = 0; i < 4; ++i)
        p.mods[(size_t) i] = { (int) get (ids::modSrc (i)), (int) get (ids::modDst (i)), get (ids::modAmt (i)) };
    p.lfoRate = get (ids::lfoRate);
    p.lfoShape = (int) get (ids::lfoShape);
    for (int i = 0; i < 8; ++i) p.steps[(size_t) i] = get (ids::step (i));
    return p;
}

void StoneProcessor::previewNote (bool on) { previewEvent.store (on ? 1 : 2); }

void StoneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0 || buffer.getNumChannels() < 2) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm()) if (*b > 1.0) bpm = *b;

    const auto p = currentParams();

    if (const int pe = previewEvent.exchange (0))
    {
        engine.setBlockOffset (0);
        if (pe == 1) engine.noteOn (60, 0.85f, p); else engine.noteOff (60);
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        engine.setBlockOffset (juce::jlimit (0, n - 1, meta.samplePosition));
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), p);
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allOff();
    }
    midi.clear();

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    engine.render (L, R, n, p);

    // grit: warm saturation with level compensation
    gritSm.target = get (ids::grit);
    for (int i = 0; i < n; ++i)
    {
        const float g = gritSm.next();
        if (g > 0.001f)
        {
            const float d = 1.0f + 9.0f * g, comp = 1.0f / std::tanh (d * 0.5f) * 0.5f;
            L[i] = std::tanh (L[i] * d) * comp;
            R[i] = std::tanh (R[i] * d) * comp;
        }
    }

    granula::FdnReverb::Params rp;
    rp.enabled = true;
    rp.mix = get (ids::verbMix);
    rp.size = get (ids::verbSize);
    rp.decaySec = get (ids::verbDecay);
    rp.damping = 0.35f;
    rp.preDelayMs = 12.0f;
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

void StoneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto stt = apvts.copyState();
    presets.writeState (stt);
    if (auto xml = stt.createXml()) copyXmlToBinary (*xml, dest);
}

void StoneProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto stt = juce::ValueTree::fromXml (*xml);
            presets.readState (stt);
            apvts.replaceState (stt);
        }
}

juce::AudioProcessorEditor* StoneProcessor::createEditor() { return new StoneEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StoneProcessor(); }
