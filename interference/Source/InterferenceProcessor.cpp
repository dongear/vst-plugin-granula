#include "InterferenceProcessor.h"
#include "InterferenceEditor.h"

using namespace interference;
using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using Attr = juce::AudioParameterFloatAttributes;

namespace
{
juce::ParameterID pid (const char* id) { return { id, 1 }; }

Attr pct()
{
    return Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
                 .withValueFromStringFunction ([] (const juce::String& s) { return s.getFloatValue() / 100.0f; });
}

Attr hz()
{
    return Attr().withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    });
}

juce::NormalisableRange<float> skew (float lo, float hi, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    juce::StringArray scaleNames;
    for (auto& s : scales()) scaleNames.add (s.name);

    l.add (std::make_unique<APC> (pid (ids::root), "Root", noteNames(), 0));
    l.add (std::make_unique<APC> (pid (ids::scale), "Scale", scaleNames, 1));
    for (int i = 0; i < 12; ++i)
        l.add (std::make_unique<APB> (pid (ids::note (i)), "Note " + noteNames()[i], false));
    l.add (std::make_unique<APB> (pid (ids::midi), "MIDI Key", false));
    l.add (std::make_unique<APC> (pid (ids::quality), "Quality", juce::StringArray { "Zero Latency", "HQ" }, 0));
    l.add (std::make_unique<APF> (pid (ids::color), "Color", juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::snap), "Snap", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::focus), "Focus", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct()));
    l.add (std::make_unique<APF> (pid (ids::gate), "Gate", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::trans), "Transients", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct()));
    l.add (std::make_unique<APF> (pid (ids::low), "Low", skew (20.0f, 2000.0f, 150.0f), 60.0f, hz()));
    l.add (std::make_unique<APF> (pid (ids::high), "High", skew (1000.0f, 20000.0f, 6000.0f), 16000.0f, hz()));
    l.add (std::make_unique<APF> (pid (ids::mix), "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct()));
    l.add (std::make_unique<APF> (pid (ids::out), "Output", juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
    return l;
}

struct Preset { const char* name; std::vector<std::pair<const char*, float>> values; };
const std::vector<Preset>& factory()
{
    using namespace ids;
    static const std::vector<Preset> p {
        { "Init", {} },
        { "Sweet Pad", { { color, 1.2f }, { focus, 0.55f }, { trans, 0.2f } } },
        { "Bass Glow", { { color, 1.6f }, { focus, 0.7f }, { low, 30.0f }, { high, 900.0f }, { trans, 0.5f } } },
        { "Vocal Snap (HQ)", { { quality, 1 }, { snap, 0.85f }, { focus, 0.4f }, { trans, 0.6f }, { low, 90.0f } } },
        { "Dissonance Fixer (HQ)", { { quality, 1 }, { snap, 1.0f }, { focus, 0.3f }, { trans, 0.4f } } },
        { "Tight Drums", { { scale, 8 }, { gate, 0.7f }, { color, 0.85f }, { trans, 0.85f } } },
        { "Shimmer Tail (HQ)", { { quality, 1 }, { color, 2.0f }, { focus, 0.8f }, { snap, 0.5f } } },
        { "Chord Follow (MIDI)", { { midi, 1 }, { color, 1.1f }, { focus, 0.6f } } },
        { "Fifths Glow", { { scale, 12 }, { color, 1.4f }, { focus, 0.6f } } },
        { "Subtle Polish", { { color, 0.45f }, { focus, 0.35f }, { trans, 0.5f } } },
    };
    return p;
}
} // namespace

InterferenceProcessor::InterferenceProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "InterferenceState", layout())
{
}

bool InterferenceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
           && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void InterferenceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    bank.prepare (sr);
    spectral.prepare (sr);
    dry.setSize (2, std::max (1, samplesPerBlock), false, true, false);
    colorW.prepare (sr, 30.0f); colorW.snap (std::min (1.0f, get (ids::color)));
    mixSm.prepare (sr, 20.0f); mixSm.snap (get (ids::mix));
    outSm.prepare (sr, 20.0f); outSm.snap (granula::dbToGain (get (ids::out)));
    hqActive = get (ids::quality) > 0.5f;
    configureLatency (hqActive);
}

void InterferenceProcessor::configureLatency (bool hq)
{
    latency = hq ? spectral.getLatency() : 0;
    dryDelay.setSize (2, std::max (1, latency), false, true, false);
    dryDelay.clear();
    dryPos = 0;
    spectral.reset();
    bank.reset();
    setLatencySamples (latency);
}

void InterferenceProcessor::reset()
{
    bank.reset();
    spectral.reset();
    dryDelay.clear();
    dryPos = 0;
    fastEnv = slowEnv = transHold = 0.0f;
}

uint16_t InterferenceProcessor::computeScaleMask() const
{
    const int sc = (int) get (ids::scale);
    if (sc >= customScaleIndex())
    {
        uint16_t m = 0;
        for (int i = 0; i < 12; ++i) if (get (ids::note (i)) > 0.5f) m |= (uint16_t) (1u << i);
        return m;
    }
    return scaleMask ((int) get (ids::root), sc);
}

void InterferenceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    // ---- quality / latency
    const bool hq = get (ids::quality) > 0.5f;
    if (hq != hqActive) { hqActive = hq; configureLatency (hq); }

    // ---- MIDI key follow: the last chord played defines the key
    const bool midiOn = get (ids::midi) > 0.5f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int pc = m.getNoteNumber() % 12;
            if (! anyHeld) midiMask = 0;
            ++heldCount[(size_t) pc];
            midiMask |= (uint16_t) (1u << pc);
            anyHeld = true;
        }
        else if (m.isNoteOff())
        {
            auto& c = heldCount[(size_t) (m.getNoteNumber() % 12)];
            c = std::max (0, c - 1);
            anyHeld = std::any_of (heldCount.begin(), heldCount.end(), [] (int v) { return v > 0; });
        }
    }
    midi.clear();
    if (! midiOn) { midiMask = 0; heldCount.fill (0); anyHeld = false; }

    const bool following = midiOn && midiMask != 0;
    const uint16_t mask = following ? midiMask : computeScaleMask();
    activeMask = mask;
    midiFollowing = following;

    // ---- parameters
    Params p;
    p.mask = mask;
    p.color = get (ids::color);
    p.snap = get (ids::snap);
    p.focus = get (ids::focus);
    p.gate = get (ids::gate);
    p.lowHz = get (ids::low);
    p.highHz = std::max (get (ids::high), p.lowHz * 1.5f);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    if (dry.getNumSamples() < n) dry.setSize (2, n, false, false, true);
    dry.copyFrom (0, 0, buffer, 0, 0, n);
    dry.copyFrom (1, 0, buffer, 1, 0, n);

    // dry aligned with the processed signal
    float* dL = dry.getWritePointer (0);
    float* dR = dry.getWritePointer (1);

    // display: input (undelayed); output is written into the same slots at the end
    displayFifo.prepareToWrite (n, displayWriteStart1, displayWriteSize1, displayWriteStart2, displayWriteSize2);
    for (int i = 0; i < displayWriteSize1; ++i) displayIn[(size_t) (displayWriteStart1 + i)] = 0.5f * (dL[i] + dR[i]);
    for (int i = 0; i < displayWriteSize2; ++i)
        displayIn[(size_t) (displayWriteStart2 + i)] = 0.5f * (dL[displayWriteSize1 + i] + dR[displayWriteSize1 + i]);

    if (hq)
    {
        spectral.process (L, R, n, p);
        colorW.target = std::min (1.0f, p.color);
        float* dl0 = dryDelay.getWritePointer (0);
        float* dl1 = dryDelay.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float a = dl0[dryPos], b = dl1[dryPos];
            dl0[dryPos] = dL[i]; dl1[dryPos] = dR[i];
            if (++dryPos >= latency) dryPos = 0;
            dL[i] = a; dR[i] = b;
            const float w = colorW.next();
            L[i] = a + (L[i] - a) * w;
            R[i] = b + (R[i] - b) * w;
        }
    }
    else
    {
        bank.process (L, R, n, p);
    }

    // ---- transients, mix, output
    const float transAmt = get (ids::trans);
    const float fa = std::exp (-1.0f / (0.0005f * (float) sr)), fr = std::exp (-1.0f / (0.015f * (float) sr));
    const float sa = std::exp (-1.0f / (0.025f * (float) sr)), srl = std::exp (-1.0f / (0.15f * (float) sr));
    const float hr = std::exp (-1.0f / (0.03f * (float) sr));
    mixSm.target = get (ids::mix);
    outSm.target = granula::dbToGain (get (ids::out));

    auto safety = [] (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        constexpr float knee = 0.9f, room = 0.1f;
        const float a = std::abs (x);
        return a <= knee ? x : std::copysign (knee + room * std::tanh ((a - knee) / room), x);
    };

    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (std::abs (dL[i]) + std::abs (dR[i]));
        fastEnv = x > fastEnv ? x + (fastEnv - x) * fa : x + (fastEnv - x) * fr;
        slowEnv = x > slowEnv ? x + (slowEnv - x) * sa : x + (slowEnv - x) * srl;
        const float t = std::clamp ((fastEnv / (slowEnv + 1.0e-6f) - 1.3f) * 1.5f, 0.0f, 1.0f) * transAmt;
        transHold = t > transHold ? t : transHold * hr;

        float yL = L[i] + (dL[i] - L[i]) * transHold;
        float yR = R[i] + (dR[i] - R[i]) * transHold;
        const float mx = mixSm.next(), g = outSm.next();
        yL = (dL[i] + (yL - dL[i]) * mx) * g;
        yR = (dR[i] + (yR - dR[i]) * mx) * g;
        L[i] = safety (yL);
        R[i] = safety (yR);
    }

    // display: output
    for (int i = 0; i < displayWriteSize1; ++i) displayOut[(size_t) (displayWriteStart1 + i)] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < displayWriteSize2; ++i)
        displayOut[(size_t) (displayWriteStart2 + i)] = 0.5f * (L[displayWriteSize1 + i] + R[displayWriteSize1 + i]);
    displayFifo.finishedWrite (displayWriteSize1 + displayWriteSize2);
}

// ---------------------------------------------------------------- key from UI

void InterferenceProcessor::setParam (const char* id, float plain)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
        p->endChangeGesture();
    }
}

void InterferenceProcessor::setScaleFromUI (int root, int scaleIndex)
{
    setParam (ids::root, (float) root);
    setParam (ids::scale, (float) scaleIndex);
    if (scaleIndex < customScaleIndex())
    {
        const auto m = scaleMask (root, scaleIndex);
        for (int i = 0; i < 12; ++i) setParam (ids::note (i), (m >> i) & 1 ? 1.0f : 0.0f);
    }
}

void InterferenceProcessor::toggleNoteFromUI (int pc)
{
    auto m = computeScaleMask();
    m ^= (uint16_t) (1u << pc);
    for (int i = 0; i < 12; ++i) setParam (ids::note (i), (m >> i) & 1 ? 1.0f : 0.0f);
    setParam (ids::scale, (float) customScaleIndex());
}

juce::String InterferenceProcessor::describeKey() const
{
    const auto m = getActiveMask();
    auto list = [m]
    {
        juce::StringArray s;
        for (int i = 0; i < 12; ++i) if ((m >> i) & 1) s.add (noteNames()[i]);
        return s.isEmpty() ? juce::String ("no notes") : s.joinIntoString (" ");
    };
    if (isFollowingMidi()) return "MIDI:  " + list();
    const int sc = (int) get (ids::scale);
    if (sc >= customScaleIndex()) return "Custom:  " + list();
    return noteNames()[(int) get (ids::root)] + " " + scales()[(size_t) sc].name;
}

// ---------------------------------------------------------------- presets

int InterferenceProcessor::getNumPrograms() { return (int) factory().size(); }

const juce::String InterferenceProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, (int) factory().size()) ? juce::String (factory()[(size_t) i].name) : juce::String();
}

void InterferenceProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) factory().size())) return;
    // Keep the user's key; a preset changes the sound, not the song's key.
    const float keepRoot = get (ids::root), keepScale = get (ids::scale);
    std::array<float, 12> keepNotes {};
    for (int i = 0; i < 12; ++i) keepNotes[(size_t) i] = get (ids::note (i));

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());

    setParam (ids::root, keepRoot);
    setParam (ids::scale, keepScale);
    for (int i = 0; i < 12; ++i) setParam (ids::note (i), keepNotes[(size_t) i]);

    for (const auto& [id, v] : factory()[(size_t) index].values) setParam (id, v);
    currentProgram = index;
    currentUserPreset.clear();
    ++presetChanges;
}

juce::File InterferenceProcessor::getUserPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("artempidor").getChildFile ("Interference Presets");
}

juce::Array<juce::File> InterferenceProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}

bool InterferenceProcessor::saveUserPreset (const juce::String& raw)
{
    const auto name = juce::File::createLegalFileName (raw.trim());
    if (name.isEmpty()) return false;
    juce::XmlElement xml ("InterferencePreset");
    xml.setAttribute ("version", JucePlugin_VersionString);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory() || ! xml.writeTo (folder.getChildFile (name + presetExtension))) return false;
    currentUserPreset = name;
    ++presetChanges;
    return true;
}

bool InterferenceProcessor::loadUserPreset (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("InterferencePreset")) return false;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            rp->setValueNotifyingHost (rp->getDefaultValue());
    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (auto* rp = apvts.getParameter (e->getStringAttribute ("id")))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("value")));
    currentUserPreset = f.getFileNameWithoutExtension();
    ++presetChanges;
    return true;
}

bool InterferenceProcessor::deleteUserPreset (const juce::File& f)
{
    if (f.getParentDirectory() != getUserPresetFolder()) return false;
    const bool ok = f.moveToTrash() || f.deleteFile();
    if (ok && currentUserPreset == f.getFileNameWithoutExtension()) currentUserPreset.clear();
    ++presetChanges;
    return ok;
}

void InterferenceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("program", currentProgram, nullptr);
    st.setProperty ("userPreset", currentUserPreset, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void InterferenceProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto st = juce::ValueTree::fromXml (*xml);
            currentProgram = st.getProperty ("program", 0);
            currentUserPreset = st.getProperty ("userPreset", {}).toString();
            apvts.replaceState (st);
            ++presetChanges;
        }
}

juce::AudioProcessorEditor* InterferenceProcessor::createEditor() { return new InterferenceEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new InterferenceProcessor(); }
