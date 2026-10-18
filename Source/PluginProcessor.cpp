#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using namespace granula;

GranulaProcessor::GranulaProcessor()
    // Instrument with an audio input: hosts show bus 1 as "Sidechain", which is
    // how INPUT mode receives audio from another track (e.g. Ableton: Sidechain -> Audio From).
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), false)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GranulaState", createParameterLayout())
{
    const char* all[] = {
        ids::mode, ids::size, ids::density, ids::sync, ids::division, ids::position, ids::spray, ids::pitch, ids::pitchRand,
        ids::scale, ids::detune, ids::reverse, ids::spread, ids::shape, ids::feedback, ids::freeze, ids::grainMix,
        ids::samplePos, ids::sampleSpray, ids::scan, ids::attack, ids::release,
        ids::tapeOn, ids::tapeDrive, ids::tapeWow, ids::tapeFlutter, ids::tapeAge, ids::tapeHiss,
        ids::delayOn, ids::delayTime, ids::delaySync, ids::delayDiv, ids::delayFb, ids::delayTone, ids::delayPing, ids::delayMix,
        ids::revOn, ids::revSize, ids::revDecay, ids::revDamp, ids::revPre, ids::revWidth, ids::revMix,
        ids::outGain
    };
    for (auto* id : all)
        if (auto* p = apvts.getRawParameterValue (id))
            params[id] = p;

    formats.registerBasicFormats();
}

GranulaProcessor::~GranulaProcessor()
{
    loaderPool.removeAllJobs (true, 10000);
}

bool GranulaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (const auto& in : layouts.inputBuses)
        if (! (in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono()))
            return false;
    return true;
}

void GranulaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    inputCopy.setSize (2, std::max (samplesPerBlock, 512), false, true, false);
    granular.prepare (sampleRate);
    sampler.prepare (sampleRate);
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
    sampler.reset();
    tape.reset();
    delay.reset();
    reverb.reset();
}

void GranulaProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        sustainedNotes[(size_t) m.getNoteNumber()] = false;
        sampler.noteOn (m.getNoteNumber(), m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        if (sustainDown) sustainedNotes[(size_t) m.getNoteNumber()] = true;
        else sampler.noteOff (m.getNoteNumber());
    }
    else if (m.isSustainPedalOn()) sustainDown = true;
    else if (m.isSustainPedalOff())
    {
        sustainDown = false;
        for (int n = 0; n < 128; ++n)
            if (sustainedNotes[(size_t) n]) { sustainedNotes[(size_t) n] = false; sampler.noteOff (n); }
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        sampler.allNotesOff();
        sustainedNotes.fill (false);
    }
}

void GranulaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                if (*b > 1.0) bpm = *b;

    const double sr = getSampleRate();
    const double beatSamples = sr * 60.0 / bpm;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    const bool synced = getBool (ids::sync);
    const float interval = synced ? (float) (beatSamples * divisionBeats (getInt (ids::division)))
                                  : (float) (sr / std::max (0.1f, get (ids::density)));

    // Grab the incoming audio (sidechain preferred, else main input) before
    // the output buffer — which shares channels with the inputs — is cleared.
    const bool inputMode = getInt (ids::mode) == 1;
    if (inputMode)
    {
        if (inputCopy.getNumSamples() < n) inputCopy.setSize (2, n, false, false, true);
        inputCopy.clear();
        const int numBuses = getBusCount (true);
        int busIndex = -1;
        for (int b = numBuses - 1; b >= 0; --b)
            if (auto* bus = getBus (true, b); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0) { busIndex = b; break; }
        if (busIndex >= 0)
        {
            auto in = getBusBuffer (buffer, true, busIndex);
            inputCopy.copyFrom (0, 0, in, 0, 0, n);
            inputCopy.copyFrom (1, 0, in, in.getNumChannels() > 1 ? 1 : 0, 0, n);
        }
    }

    buffer.clear();

    // Notes from the on-screen preview
    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        auto apply = [this] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const int e = previewEvents[(size_t) (start + i)];
                if (e >> 8) sampler.noteOn (e & 0x7f, 0.8f); else sampler.noteOff (e & 0x7f);
            }
        };
        apply (scope.startIndex1, scope.blockSize1);
        apply (scope.startIndex2, scope.blockSize2);
    }

    if (! inputMode)
    {
        GranularSampler::Params sp;
        sp.sizeMs = get (ids::size);
        sp.intervalSamples = interval;
        sp.synced = synced;
        sp.position = get (ids::samplePos);
        sp.spray = get (ids::sampleSpray);
        sp.scan = get (ids::scan);
        sp.pitchSemis = get (ids::pitch);
        sp.pitchRandSemis = get (ids::pitchRand);
        sp.scale = getInt (ids::scale);
        sp.detuneCents = get (ids::detune);
        sp.reverseProb = get (ids::reverse);
        sp.spread = get (ids::spread);
        sp.shape = get (ids::shape);
        sp.attackMs = get (ids::attack);
        sp.releaseMs = get (ids::release);

        const juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked())
        {
            const SampleData* s = sample.get();
            int pos = 0;
            for (const auto meta : midi)
            {
                const int t = juce::jlimit (0, n, meta.samplePosition);
                if (t > pos) { sampler.render (L + pos, R + pos, t - pos, sp, s); pos = t; }
                handleMidi (meta.getMessage());
            }
            if (pos < n) sampler.render (L + pos, R + pos, n - pos, sp, s);
        }
        else
        {
            for (const auto meta : midi) handleMidi (meta.getMessage()); // sample is being swapped: stay silent
        }
    }
    else
    {
        for (const auto meta : midi) handleMidi (meta.getMessage()); // keep note state consistent
        buffer.copyFrom (0, 0, inputCopy, 0, 0, n);
        buffer.copyFrom (1, 0, inputCopy, 1, 0, n);

        GranularEngine::Params gp;
        gp.sizeMs = get (ids::size);
        gp.synced = synced;
        gp.intervalSamples = interval;
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
    }
    midi.clear();

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

// ---------------------------------------------------------------- sample loading

bool GranulaProcessor::isSupportedFile (const juce::File& f) const
{
    return f.existsAsFile() && supportedFileWildcard().containsIgnoreCase (f.getFileExtension());
}

bool GranulaProcessor::decodeFile (const juce::File& file, SampleData& out, juce::String& error)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) { error = "Can't read this file format"; return false; }

    constexpr double maxSeconds = 300.0; // 5 minutes keeps memory reasonable
    const auto total = (juce::int64) std::min ((double) reader->lengthInSamples, maxSeconds * reader->sampleRate);
    if (total < 64) { error = "File is empty"; return false; }

    juce::AudioBuffer<float> tmp ((int) std::max (1u, std::min (2u, reader->numChannels)), (int) total);
    if (! reader->read (&tmp, 0, (int) total, 0, true, tmp.getNumChannels() > 1))
    {
        error = "Error while decoding file";
        return false;
    }

    out.left.assign (tmp.getReadPointer (0), tmp.getReadPointer (0) + total);
    const int rch = tmp.getNumChannels() > 1 ? 1 : 0;
    out.right.assign (tmp.getReadPointer (rch), tmp.getReadPointer (rch) + total);
    out.sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    out.name = file.getFileName().toStdString();
    out.buildPeaks();
    return true;
}

void GranulaProcessor::loadSampleAsync (const juce::File& file)
{
    loading = true;
    loaderPool.addJob ([this, file]
    {
        auto data = std::make_shared<SampleData>();
        juce::String error;
        if (decodeFile (file, *data, error))
        {
            {
                const juce::SpinLock::ScopedLockType lock (sampleLock);
                std::swap (sample, data);
            }
            data.reset(); // old sample freed here, never on the audio thread
            const juce::ScopedLock sl (errorLock);
            sampleError.clear();
            loadedPath = file.getFullPathName();
        }
        else
        {
            const juce::ScopedLock sl (errorLock);
            sampleError = error + ": " + file.getFileName();
        }
        loading = false;
        ++sampleVersion;
    });
}

std::shared_ptr<const SampleData> GranulaProcessor::getSample() const
{
    const juce::SpinLock::ScopedLockType lock (sampleLock);
    return sample;
}

juce::String GranulaProcessor::getSampleError() const
{
    const juce::ScopedLock sl (errorLock);
    return sampleError;
}

void GranulaProcessor::previewNote (int note, bool on)
{
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0) previewEvents[(size_t) scope.startIndex1] = (note & 0x7f) | ((on ? 1 : 0) << 8);
    else if (scope.blockSize2 > 0) previewEvents[(size_t) scope.startIndex2] = (note & 0x7f) | ((on ? 1 : 0) << 8);
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
    currentUserPreset.clear();
    ++presetChangeCount;

    // Reset everything (except Freeze) to defaults, then apply the preset's values.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ids::freeze)
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (const auto& [id, value] : presets[(size_t) index].values)
        if (auto* rp = apvts.getParameter (id))
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
}

// ---------------------------------------------------------------- user presets

juce::File GranulaProcessor::getUserPresetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("artempidor")
               .getChildFile ("Presets");
}

juce::Array<juce::File> GranulaProcessor::getUserPresets() const
{
    auto files = getUserPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + presetExtension);
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });
    return files;
}

bool GranulaProcessor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;

    juce::XmlElement xml ("GranulaPreset");
    xml.setAttribute ("plugin", JucePlugin_Name);
    xml.setAttribute ("version", JucePlugin_VersionString);
    xml.setAttribute ("name", name);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->getParameterID() == ids::freeze) continue;
            auto* e = xml.createNewChildElement ("PARAM");
            e->setAttribute ("id", rp->getParameterID());
            e->setAttribute ("value", (double) rp->convertFrom0to1 (rp->getValue()));
        }
    {
        const juce::ScopedLock sl (errorLock);
        if (loadedPath.isNotEmpty()) xml.setAttribute ("samplePath", loadedPath);
    }

    auto folder = getUserPresetFolder();
    if (! folder.createDirectory()) return false;
    if (! xml.writeTo (folder.getChildFile (name + presetExtension))) return false;
    currentUserPreset = name;
    ++presetChangeCount;
    return true;
}

bool GranulaProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("GranulaPreset")) return false;

    // Start from defaults so presets from older versions stay predictable.
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != ids::freeze)
                rp->setValueNotifyingHost (rp->getDefaultValue());

    for (auto* e : xml->getChildWithTagNameIterator ("PARAM"))
        if (auto* rp = apvts.getParameter (e->getStringAttribute ("id")))
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) e->getDoubleAttribute ("value")));

    {
        const juce::File sampleFile (xml->getStringAttribute ("samplePath"));
        if (xml->hasAttribute ("samplePath") && sampleFile.existsAsFile()) loadSampleAsync (sampleFile);
    }

    currentUserPreset = file.getFileNameWithoutExtension();
    ++presetChangeCount;
    return true;
}

bool GranulaProcessor::deleteUserPreset (const juce::File& file)
{
    if (file.getParentDirectory() != getUserPresetFolder()) return false;
    const bool ok = file.moveToTrash() || file.deleteFile();
    if (ok && currentUserPreset == file.getFileNameWithoutExtension()) currentUserPreset.clear();
    ++presetChangeCount;
    return ok;
}

// ---------------------------------------------------------------- state

void GranulaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("userPreset", currentUserPreset, nullptr);
    {
        const juce::ScopedLock sl (errorLock);
        state.setProperty ("samplePath", loadedPath, nullptr);
    }
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
            currentUserPreset = state.getProperty ("userPreset", {}).toString();
            ++presetChangeCount;
            const juce::String path = state.getProperty ("samplePath", {}).toString();
            apvts.replaceState (state);

            if (path.isNotEmpty())
            {
                const juce::File f (path);
                if (f.existsAsFile()) loadSampleAsync (f);
                else
                {
                    const juce::ScopedLock sl (errorLock);
                    loadedPath = path; // keep the reference so re-saving doesn't lose it
                    sampleError = "Sample not found: " + path;
                    ++sampleVersion;
                }
            }
        }
}

juce::AudioProcessorEditor* GranulaProcessor::createEditor() { return new GranulaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new GranulaProcessor(); }
