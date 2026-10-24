#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "StoneDsp.h"
#include "../../Source/dsp/FdnReverb.h"
#include "../../common/Presets.h"

namespace stone::ids
{
inline constexpr const char* material = "material";
inline constexpr const char* exciter  = "exciter";
inline constexpr const char* hardness = "hardness";
inline constexpr const char* rough    = "rough";
inline constexpr const char* decay    = "decay";
inline constexpr const char* bright   = "bright";
inline constexpr const char* damp     = "damp";
inline constexpr const char* pitch    = "pitch";
inline constexpr const char* inharm   = "inharm";
inline constexpr const char* click    = "click";
inline constexpr const char* hits     = "hits";
inline constexpr const char* rate     = "rate";
inline constexpr const char* sync     = "sync";
inline constexpr const char* div      = "div";
inline constexpr const char* accel    = "accel";
inline constexpr const char* jitter   = "jitter";
inline constexpr const char* density  = "density";
inline constexpr const char* hitDecay = "hitdecay";
inline constexpr const char* drift    = "drift";
inline constexpr const char* spread   = "spread";
inline constexpr const char* hold     = "hold";
inline constexpr const char* lfoRate  = "lforate";
inline constexpr const char* lfoShape = "lfoshape";
inline constexpr const char* grit     = "grit";
inline constexpr const char* verbMix  = "verbmix";
inline constexpr const char* verbSize = "verbsize";
inline constexpr const char* verbDecay = "verbdecay";
inline constexpr const char* out      = "output";
inline std::string modSrc (int i) { return "m" + std::to_string (i + 1) + "src"; }
inline std::string modDst (int i) { return "m" + std::to_string (i + 1) + "dst"; }
inline std::string modAmt (int i) { return "m" + std::to_string (i + 1) + "amt"; }
inline std::string step (int i) { return "step" + std::to_string (i + 1); }
} // namespace stone::ids

class StoneProcessor : public juce::AudioProcessor
{
public:
    StoneProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); reverb.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return presets.numFactory(); }
    int getCurrentProgram() override { return presets.currentFactory(); }
    void setCurrentProgram (int i) override { presets.loadFactory (i); }
    const juce::String getProgramName (int i) override { return presets.factoryName (i); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    common::PresetManager presets;
    stone::Engine engine;

    // the trail as currently configured (for the UI preview)
    stone::EngineParams currentParams() const;
    void previewNote (bool on);

private:
    float get (const std::string& id) const { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }

    granula::FdnReverb reverb;
    granula::Smoother outSm, gritSm;
    double sr = 48000.0, bpm = 120.0;
    std::atomic<int> previewEvent { 0 }; // 1 = on, 2 = off

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StoneProcessor)
};
