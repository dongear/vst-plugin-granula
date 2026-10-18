#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace granula;

GranulaProcessor::GranulaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GranulaState", createParameterLayout())
{
    const char* all[] = {
        ids::size, ids::density, ids::sync, ids::division, ids::position, ids::spray, ids::pitch, ids::pitchRand,
        ids::scale, ids::detune, ids::reverse, ids::spread, ids::shape, ids::feedback, ids::freeze, ids::grainMix,
        ids::tapeOn, ids::tapeDrive, ids::tapeWow, ids::tapeFlutter, ids::tapeAge, ids::tapeHiss,
        ids::delayOn, ids::delayTime, ids::delaySync, ids::delayDiv, ids::delayFb, ids::delayTone, ids::delayPing, ids::delayMix,
        ids::revOn, ids::revSize, ids::revDecay, ids::revDamp, ids::revPre, ids::revWidth, ids::revMix,
        ids::outGain
    };
    for (auto* id : all)
    {
        auto* p = apvts.getRawParameterValue (id);
        jassert (p != nullptr);
        params[id] = p;
    }
}

bool GranulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void GranulaProcessor::prepareToPlay (double sampleRate, int)
{
    granular.prepare (sampleRate);
    tape.prepare (sampleRate);
    delay.prepare (sampleRate);
    reverb.prepare (sampleRate);
    outSm.prepare (sampleRate, 20.0f);
    outSm.snap (dbToGain (get (ids::outGain)));
    setLatencySamples (tape.getLatencySamples());
}

void GranulaProcessor::reset()
{
    granular.reset();
    tape.reset();
    delay.reset();
    reverb.reset();
}

void GranulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();

    if (buffer.getNumChannels() < 2 || n == 0) return;
    if (numIn == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    else if (numIn == 0) buffer.clear();

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 1.0) bpm = *b;

    const double sr = getSampleRate();
    const double beatSamples = sr * 60.0 / bpm;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- Grain ----
    GranularEngine::Params gp;
    gp.sizeMs = get (ids::size);
    gp.synced = getBool (ids::sync);
    gp.intervalSamples = gp.synced ? (float) (beatSamples * divisionBeats (getInt (ids::division)))
                                   : (float) (sr / std::max (0.1f, get (ids::density)));
    gp.positionMs = get (ids::position);
    gp.sprayMs = get (ids::spray);
    gp.pitchSemis = get (ids::pitch);
    gp.pitchRandSemis = get (ids::pitchRand);
    gp.scale = getInt (ids::scale);
    gp.detuneCents = get (ids::detune);
    gp.reverseProb = get (ids::reverse);
    gp.spread = get (ids::spread);
    gp.shape = get (ids::shape);
    gp.feedback = get (ids::feedback);
    gp.freeze = getBool (ids::freeze);
    gp.mix = get (ids::grainMix);
    granular.process (L, R, n, gp);

    // ---- Tape ----
    TapeSaturation::Params tp;
    tp.enabled = getBool (ids::tapeOn);
    tp.driveDb = get (ids::tapeDrive);
    tp.wow = get (ids::tapeWow);
    tp.flutter = get (ids::tapeFlutter);
    tp.age = get (ids::tapeAge);
    tp.hiss = get (ids::tapeHiss);
    tape.process (L, R, n, tp);

    // ---- Delay ----
    StereoDelay::Params dp;
    dp.enabled = getBool (ids::delayOn);
    dp.timeMs = getBool (ids::delaySync) ? (float) (60000.0 / bpm * divisionBeats (getInt (ids::delayDiv)))
                                         : get (ids::delayTime);
    dp.feedback = get (ids::delayFb);
    dp.toneHz = get (ids::delayTone);
    dp.pingPong = getBool (ids::delayPing);
    dp.mix = get (ids::delayMix);
    delay.process (L, R, n, dp);

    // ---- Reverb ----
    FdnReverb::Params rp;
    rp.enabled = getBool (ids::revOn);
    rp.size = get (ids::revSize);
    rp.decaySec = get (ids::revDecay);
    rp.damping = get (ids::revDamp);
    rp.preDelayMs = get (ids::revPre);
    rp.width = get (ids::revWidth);
    rp.mix = get (ids::revMix);
    reverb.process (L, R, n, rp);

    // ---- Output: gain + transparent-below-knee safety clipper ----
    auto safety = [] (float x)
    {
        if (! std::isfinite (x)) return 0.0f;
        constexpr float knee = 0.85f, room = 1.0f - knee;
        const float a = std::abs (x);
        if (a <= knee) return x;
        return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
    };

    outSm.target = dbToGain (get (ids::outGain));
    for (int i = 0; i < n; ++i)
    {
        const float g = outSm.next();
        L[i] = safety (L[i] * g);
        R[i] = safety (R[i] * g);
    }
}

// ---------------------------------------------------------------- presets

int GranulaProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String GranulaProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String (p[(size_t) index].name) : juce::String();
}

void GranulaProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    currentProgram = index;

    // Reset everything (except Freeze) to defaults, then apply the preset's values.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ids::freeze)
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

// ---------------------------------------------------------------- state

void GranulaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GranulaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = state.getProperty ("program", 0);
            apvts.replaceState (state);
        }
}

juce::AudioProcessorEditor* GranulaProcessor::createEditor() { return new GranulaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GranulaProcessor(); }
