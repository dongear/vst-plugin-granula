#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace granula::ids
{
// Grain
inline constexpr const char* size      = "g_size";
inline constexpr const char* density   = "g_density";
inline constexpr const char* sync      = "g_sync";
inline constexpr const char* division  = "g_div";
inline constexpr const char* position  = "g_pos";
inline constexpr const char* spray     = "g_spray";
inline constexpr const char* pitch     = "g_pitch";
inline constexpr const char* pitchRand = "g_prand";
inline constexpr const char* scale     = "g_scale";
inline constexpr const char* detune    = "g_detune";
inline constexpr const char* reverse   = "g_reverse";
inline constexpr const char* spread    = "g_spread";
inline constexpr const char* shape     = "g_shape";
inline constexpr const char* feedback  = "g_feedback";
inline constexpr const char* freeze    = "g_freeze";
inline constexpr const char* grainMix  = "g_mix";
// Tape
inline constexpr const char* tapeOn      = "t_on";
inline constexpr const char* tapeDrive   = "t_drive";
inline constexpr const char* tapeWow     = "t_wow";
inline constexpr const char* tapeFlutter = "t_flutter";
inline constexpr const char* tapeAge     = "t_age";
inline constexpr const char* tapeHiss    = "t_hiss";
// Delay
inline constexpr const char* delayOn   = "d_on";
inline constexpr const char* delayTime = "d_time";
inline constexpr const char* delaySync = "d_sync";
inline constexpr const char* delayDiv  = "d_div";
inline constexpr const char* delayFb   = "d_fb";
inline constexpr const char* delayTone = "d_tone";
inline constexpr const char* delayPing = "d_ping";
inline constexpr const char* delayMix  = "d_mix";
// Reverb
inline constexpr const char* revOn    = "r_on";
inline constexpr const char* revSize  = "r_size";
inline constexpr const char* revDecay = "r_decay";
inline constexpr const char* revDamp  = "r_damp";
inline constexpr const char* revPre   = "r_pre";
inline constexpr const char* revWidth = "r_width";
inline constexpr const char* revMix   = "r_mix";
// Output
inline constexpr const char* outGain = "out_gain";
} // namespace granula::ids

namespace granula
{
inline const juce::StringArray& divisionNames()
{
    static const juce::StringArray names { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T", "1/4 D", "1/8 D" };
    return names;
}

// Length of a note division in quarter-note beats.
inline double divisionBeats (int index)
{
    static const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75 };
    return beats[juce::jlimit (0, 10, index)];
}

inline const juce::StringArray& scaleNames()
{
    static const juce::StringArray names { "Free", "Chromatic", "Major", "Minor", "Pentatonic", "Fifths", "Octaves" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
} // namespace granula
