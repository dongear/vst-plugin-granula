#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HarmonicDsp.h"

namespace interference::ids
{
inline constexpr const char* root    = "root";
inline constexpr const char* scale   = "scale";
inline constexpr const char* midi    = "midi";
inline constexpr const char* quality = "quality";
inline constexpr const char* color   = "color";
inline constexpr const char* snap    = "snap";
inline constexpr const char* focus   = "focus";
inline constexpr const char* gate    = "gate";
inline constexpr const char* trans   = "transients";
inline constexpr const char* low     = "low";
inline constexpr const char* high    = "high";
inline constexpr const char* mix     = "mix";
inline constexpr const char* out     = "output";
inline const char* note (int pc)
{
    static const char* n[12] = { "n0", "n1", "n2", "n3", "n4", "n5", "n6", "n7", "n8", "n9", "n10", "n11" };
    return n[pc];
}
} // namespace interference::ids

namespace interference
{
inline const juce::StringArray& noteNames()
{
    static const juce::StringArray n { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return n;
}

struct ScaleDef { const char* name; std::vector<int> steps; };
inline const std::vector<ScaleDef>& scales()
{
    static const std::vector<ScaleDef> s {
        { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Major Chord", { 0, 4, 7 } },
        { "Minor Chord", { 0, 3, 7 } },
        { "Sus4 Chord", { 0, 5, 7 } },
        { "Fifth", { 0, 7 } },
        { "Octave", { 0 } },
        { "Custom", {} },
    };
    return s;
}
inline int customScaleIndex() { return (int) scales().size() - 1; }

inline uint16_t scaleMask (int root, int scaleIndex)
{
    uint16_t m = 0;
    for (int st : scales()[(size_t) juce::jlimit (0, customScaleIndex(), scaleIndex)].steps)
        m |= (uint16_t) (1u << ((root + st) % 12));
    return m;
}
} // namespace interference

class InterferenceProcessor : public juce::AudioProcessor
{
public:
    InterferenceProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- key handling (UI)
    uint16_t getActiveMask() const { return activeMask.load(); }
    bool isFollowingMidi() const { return midiFollowing.load(); }
    void setScaleFromUI (int root, int scaleIndex);
    void toggleNoteFromUI (int pitchClass);
    juce::String describeKey() const;

    // ---- user presets (Documents/artempidor/Interference Presets/*.interference)
    juce::File getUserPresetFolder() const;
    juce::Array<juce::File> getUserPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File&);
    bool deleteUserPreset (const juce::File&);
    juce::String getUserPresetName() const { return currentUserPreset; }
    int getPresetChangeCount() const { return presetChanges.load(); }
    static constexpr const char* presetExtension = ".interference";

    // ---- spectrum display: mono input / output pushed here, read by the UI
    juce::AbstractFifo displayFifo { 1 << 15 };
    std::vector<float> displayIn = std::vector<float> (1 << 15), displayOut = std::vector<float> (1 << 15);

private:
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load (std::memory_order_relaxed); }
    void setParam (const char* id, float plainValue);
    uint16_t computeScaleMask() const;
    void configureLatency (bool hq);

    interference::FilterBank bank;
    interference::SpectralEngine spectral;
    bool hqActive = false;
    int latency = 0;

    juce::AudioBuffer<float> dryDelay;   // circular, holds `latency` samples
    int dryPos = 0;
    juce::AudioBuffer<float> dry;

    granula::Smoother colorW, mixSm, outSm;
    int displayWriteStart1 = 0, displayWriteSize1 = 0, displayWriteStart2 = 0, displayWriteSize2 = 0;
    float fastEnv = 0, slowEnv = 0, transHold = 0;
    double sr = 48000.0;

    std::array<int, 12> heldCount {};
    uint16_t midiMask = 0;
    bool anyHeld = false;
    std::atomic<uint16_t> activeMask { 0 };
    std::atomic<bool> midiFollowing { false };

    int currentProgram = 0;
    juce::String currentUserPreset;
    std::atomic<int> presetChanges { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterferenceProcessor)
};
