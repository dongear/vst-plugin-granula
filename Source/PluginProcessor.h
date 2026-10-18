#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/GranularEngine.h"
#include "dsp/TapeSaturation.h"
#include "dsp/StereoDelay.h"
#include "dsp/FdnReverb.h"

class GranulaProcessor : public juce::AudioProcessor
{
public:
    GranulaProcessor();
    ~GranulaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
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
    granula::GranularEngine& getGranular() { return granular; }

private:
    float get (const char* id) const { return params.at (id)->load (std::memory_order_relaxed); }
    bool getBool (const char* id) const { return get (id) > 0.5f; }
    int getInt (const char* id) const { return (int) std::lround (get (id)); }

    // Keyed by the id pointer itself (ids are inline constants) → no allocation on the audio thread.
    std::map<const char*, std::atomic<float>*> params;

    granula::GranularEngine granular;
    granula::TapeSaturation tape;
    granula::StereoDelay delay;
    granula::FdnReverb reverb;
    granula::Smoother outSm;

    double bpm = 120.0;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GranulaProcessor)
};
