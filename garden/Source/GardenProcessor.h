#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "GardenDsp.h"
#include "../../Source/dsp/FdnReverb.h"
#include "../../Source/dsp/TapeSaturation.h"
#include "../../common/Presets.h"

namespace garden::ids
{
inline constexpr const char* seed     = "seed";
inline constexpr const char* growth   = "growth";
inline constexpr const char* branch   = "branch";
inline constexpr const char* bend     = "bend";
inline constexpr const char* leaves   = "leaves";
inline constexpr const char* roots    = "roots";
inline constexpr const char* sun      = "sun";
inline constexpr const char* width    = "width";
inline constexpr const char* water    = "water";
inline constexpr const char* wind     = "wind";
inline constexpr const char* season   = "season";
inline constexpr const char* bloom    = "bloom";
inline constexpr const char* wither   = "wither";
inline constexpr const char* touch    = "touch";
inline constexpr const char* touchMat = "touchmat";
inline constexpr const char* octave   = "octave";
inline constexpr const char* chorus   = "chorus";
inline constexpr const char* space    = "space";
inline constexpr const char* spaceSize = "spacesize";
inline constexpr const char* warmth   = "warmth";
inline constexpr const char* out      = "output";
} // namespace garden::ids

class GardenProcessor : public juce::AudioProcessor
{
public:
    GardenProcessor();

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
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return presets.numFactory(); }
    int getCurrentProgram() override { return presets.currentFactory(); }
    void setCurrentProgram (int i) override { presets.loadFactory (i); }
    const juce::String getProgramName (int i) override { return presets.factoryName (i); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    common::PresetManager presets;
    garden::Engine engine;

    garden::PlantParams plantParams() const;
    void previewNote (bool on) { previewEvent.store (on ? 1 : 2); }

private:
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }

    garden::Chorus chorus;
    granula::FdnReverb reverb;
    granula::TapeSaturation tape;
    granula::Smoother outSm;
    garden::PlantParams lastPlant;
    bool plantValid = false;
    float modWheel = 0.0f, bendSemis = 0.0f;
    std::atomic<int> previewEvent { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GardenProcessor)
};
