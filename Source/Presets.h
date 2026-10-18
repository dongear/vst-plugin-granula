#pragma once

#include "Parameters.h"
#include <vector>
#include <utility>

namespace granula
{
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // plain (un-normalised) values; others use defaults
};

inline const std::vector<Preset>& factoryPresets()
{
    using namespace ids;
    static const std::vector<Preset> presets {
        { "Init", {} },
        { "Cloud Pad", {
            { size, 420 }, { density, 24 }, { position, 300 }, { spray, 450 }, { pitchRand, 12 }, { scale, 6 },
            { detune, 10 }, { reverse, 0.3f }, { spread, 1.0f }, { shape, 0.05f }, { feedback, 0.3f }, { grainMix, 0.85f },
            { tapeDrive, 4 }, { tapeWow, 0.3f }, { tapeAge, 0.3f },
            { delayMix, 0.18f }, { revSize, 0.9f }, { revDecay, 8 }, { revMix, 0.5f } } },
        { "Shimmer Octaves", {
            { size, 260 }, { density, 20 }, { position, 120 }, { spray, 200 }, { pitch, 12 }, { pitchRand, 12 }, { scale, 6 },
            { detune, 8 }, { spread, 0.9f }, { shape, 0.1f }, { feedback, 0.45f }, { grainMix, 0.6f },
            { tapeDrive, 2 }, { delayFb, 0.5f }, { delayMix, 0.25f }, { revSize, 0.85f }, { revDecay, 10 }, { revMix, 0.45f } } },
        { "Glitch Stutter", {
            { size, 45 }, { sync, 1 }, { division, 4 }, { position, 0 }, { spray, 120 }, { pitchRand, 7 }, { scale, 5 },
            { reverse, 0.5f }, { spread, 0.8f }, { shape, 0.9f }, { grainMix, 0.75f },
            { tapeDrive, 12 }, { delayOn, 0 }, { revMix, 0.15f } } },
        { "Lo-Fi Cassette", {
            { size, 150 }, { density, 10 }, { grainMix, 0.3f },
            { tapeDrive, 14 }, { tapeWow, 0.7f }, { tapeFlutter, 0.5f }, { tapeAge, 0.7f }, { tapeHiss, 0.35f },
            { delayMix, 0.2f }, { revMix, 0.2f } } },
        { "Reverse Dream", {
            { size, 600 }, { density, 8 }, { position, 500 }, { spray, 200 }, { reverse, 1.0f }, { detune, 12 },
            { spread, 0.8f }, { shape, 0.0f }, { grainMix, 0.7f },
            { delayFb, 0.45f }, { delayMix, 0.3f }, { revDecay, 6 }, { revMix, 0.4f } } },
        { "Drone Machine", {
            { size, 900 }, { density, 30 }, { position, 200 }, { spray, 1000 }, { pitchRand, 5 }, { scale, 2 },
            { detune, 14 }, { spread, 1.0f }, { shape, 0.0f }, { feedback, 0.6f }, { grainMix, 0.9f },
            { tapeWow, 0.4f }, { tapeAge, 0.4f }, { revSize, 1.0f }, { revDecay, 15 }, { revMix, 0.6f } } },
        { "Grain Delay", {
            { size, 80 }, { density, 40 }, { position, 250 }, { spray, 10 }, { detune, 15 }, { feedback, 0.5f },
            { grainMix, 0.5f }, { delayOn, 0 }, { revMix, 0.2f } } },
    };
    return presets;
}
} // namespace granula
