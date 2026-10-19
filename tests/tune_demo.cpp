// Writes before/after WAVs of the auto-tune on a synthetic, slightly off-key "voice".
#include "../interference/Source/AutoTune.h"
#include <cstdio>
#include <fstream>
using namespace interference;

static void wav (const std::string& path, const std::vector<float>& x, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t n = (uint32_t) x.size();
    f.write ("RIFF", 4); u32 (36 + n * 2); f.write ("WAVE", 4); f.write ("fmt ", 4); u32 (16); u16 (1); u16 (1);
    u32 ((uint32_t) sr); u32 ((uint32_t) sr * 2); u16 (2); u16 (16); f.write ("data", 4); u32 (n * 2);
    for (float v : x) { int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f); f.write ((const char*) &s, 2); }
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const int sr = 48000;
    // melody in C major, sung a bit off: (pitch, seconds)
    const std::vector<std::pair<double, double>> mel = { { 60.35, 0.6 }, { 61.6, 0.6 }, { 64.3, 0.6 }, { 64.8, 0.4 }, { 67.45, 0.9 },
                                                         { 65.3, 0.5 }, { 63.7, 0.5 }, { 62.2, 0.6 }, { 59.6, 1.2 } };
    std::vector<float> x;
    double ph = 0, cur = mel[0].first, t = 0;
    granula::Biquad f1, f2, f3;
    f1.setLowpass (sr, 3200); f2.setHighpass (sr, 120);
    for (const auto& [p, dur] : mel)
    {
        const int n = (int) (dur * sr);
        for (int i = 0; i < n; ++i, t += 1.0 / sr)
        {
            cur += (p - cur) * 0.0015; // glide between notes
            const double vib = 0.25 * std::sin (2.0 * kPi * 5.5 * t) * std::min (1.0, i / (0.3 * sr));
            const double f = noteFreq (cur + vib);
            ph += f / sr;
            double s = 0;
            for (int h = 1; h <= 25 && h * f < 8000; ++h)
            {
                const double hf = h * f; // crude vowel "ah": formants ~700 / 1200 Hz
                const double fm = 1.0 / (1.0 + std::pow ((hf - 700) / 250, 2)) + 0.6 / (1.0 + std::pow ((hf - 1200) / 300, 2)) + 0.15;
                s += std::sin (2.0 * kPi * h * ph) * fm / std::sqrt ((double) h);
            }
            const double env = std::min (1.0, i / (0.03 * sr)) * std::min (1.0, (n - i) / (0.05 * sr));
            x.push_back ((float) (f2.process (f1.process ((float) s)) * 0.18 * env));
        }
    }
    wav (dir + "/tune_0_input.wav", x, sr);

    auto run = [&] (PitchCorrector::Params p, const char* name)
    {
        PitchCorrector pc; pc.prepare (sr);
        std::vector<float> l = x, r = x;
        for (size_t pos = 0; pos < x.size(); pos += 256)
            pc.process (l.data() + pos, r.data() + pos, (int) std::min<size_t> (256, x.size() - pos), p);
        l.erase (l.begin(), l.begin() + pc.getLatency());
        wav (dir + "/" + name, l, sr);
    };
    PitchCorrector::Params hard; hard.speedMs = 0;
    PitchCorrector::Params nat; nat.speedMs = 45; nat.humanize = 0.5f;
    run (hard, "tune_1_hard.wav");
    run (nat, "tune_2_natural.wav");
    std::puts ("ok");
}
