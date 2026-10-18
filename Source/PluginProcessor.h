#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Parameters.h"
#include "dsp/GranularEngine.h"
#include "dsp/GranularSampler.h"
#include "dsp/TapeSaturation.h"
#include "dsp/StereoDelay.h"
#include "dsp/FdnReverb.h"

// One processor class, built twice:
//  - "Granula"    (JucePlugin_IsSynth = 1): MIDI instrument that granulates a loaded sample
//  - "Granula FX" (JucePlugin_IsSynth = 0): effect that granulates the live input
class GranulaProcessor : public juce::AudioProcessor
{
public:
    static constexpr bool isInstrument = JucePlugin_IsSynth != 0;

    GranulaProcessor();
    ~GranulaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return isInstrument; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- live-input engine (FX build)
    granula::GranularEngine& getGranular() { return granular; }

    // ---- sampler (instrument build)
    granula::GranularSampler& getSampler() { return sampler; }
    void loadSampleAsync (const juce::File& file);
    std::shared_ptr<const granula::SampleData> getSample() const;
    juce::String getSampleError() const;
    bool isLoadingSample() const { return loading.load(); }
    int getSampleVersion() const { return sampleVersion.load(); }
    static juce::String supportedFileWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a"; }
    bool isSupportedFile (const juce::File& f) const;

    // ---- user presets (files in ~/Documents/Granula Presets/<plugin>/)
    static constexpr const char* presetExtension = ".granula";
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    bool deleteUserPreset (const juce::File& file);
    juce::String getUserPresetName() const { return currentUserPreset; } // empty = factory preset active
    int getPresetChangeCount() const { return presetChangeCount.load(); }

    // Lets the UI audition the sampler without a MIDI keyboard.
    void previewNote (int note, bool on);

private:
    float get (const char* id) const
    {
        auto it = params.find (id);
        return it != params.end() ? it->second->load (std::memory_order_relaxed) : 0.0f;
    }
    bool getBool (const char* id) const { return get (id) > 0.5f; }
    int getInt (const char* id) const { return (int) std::lround (get (id)); }

    void handleMidi (const juce::MidiMessage& m);
    bool decodeFile (const juce::File& file, granula::SampleData& out, juce::String& error);

    // Keyed by the id pointer itself (ids are inline constants) → no allocation on the audio thread.
    std::map<const char*, std::atomic<float>*> params;

    granula::GranularEngine granular;
    granula::GranularSampler sampler;
    granula::TapeSaturation tape;
    granula::StereoDelay delay;
    granula::FdnReverb reverb;
    granula::Smoother outSm;

    // Sample handling: the audio thread only *borrows* the pointer under a
    // try-lock; the loader thread swaps it and frees the old one itself.
    mutable juce::SpinLock sampleLock;
    std::shared_ptr<granula::SampleData> sample;
    juce::AudioFormatManager formats;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<bool> loading { false };
    std::atomic<int> sampleVersion { 0 };
    mutable juce::CriticalSection errorLock;
    juce::String sampleError;
    juce::String loadedPath;

    // MIDI state
    bool sustainDown = false;
    std::array<bool, 128> sustainedNotes {};
    juce::AbstractFifo previewFifo { 64 };
    std::array<int, 64> previewEvents {}; // note | (on << 8)

    double bpm = 120.0;
    int currentProgram = 0;
    juce::String currentUserPreset;
    std::atomic<int> presetChangeCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranulaProcessor)
};
