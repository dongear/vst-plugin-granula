// GARDEN engine checks + demo renders.
#include "../garden/Source/GardenDsp.h"
#include "../Source/dsp/FdnReverb.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
using namespace garden;

static void wav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t n = (uint32_t) L.size();
    f.write ("RIFF", 4); u32 (36 + n * 4); f.write ("WAVE", 4); f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2);
    u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16); f.write ("data", 4); u32 (n * 4);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { L[i], R[i] }) { int16_t s = (int16_t) std::lround (std::clamp (v, -1.0f, 1.0f) * 32767.0f); f.write ((const char*) &s, 2); }
}

struct Ev { double t; int note; float vel; bool on; };

static std::pair<std::vector<float>, std::vector<float>> play (const PlantParams& pp, const VoiceParams& vp, const std::vector<Ev>& evs, double secs, double* cpu = nullptr)
{
    const int sr = 48000, B = 256, n = (int) (secs * sr);
    Engine e; e.prepare (sr); e.setPlant (buildPlant (pp));
    granula::FdnReverb rv; rv.prepare (sr);
    Chorus ch; ch.prepare (sr);
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    size_t ei = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int pos = 0; pos < n; pos += B)
    {
        const int len = std::min (B, n - pos);
        while (ei < evs.size() && evs[ei].t * sr < pos + len) { if (evs[ei].on) e.noteOn (evs[ei].note, evs[ei].vel); else e.noteOff (evs[ei].note); ++ei; }
        e.render (L.data() + pos, R.data() + pos, len, vp);
        ch.process (L.data() + pos, R.data() + pos, len, 0.35f);
        granula::FdnReverb::Params rp; rp.mix = 0.3f; rp.decaySec = 4.0f; rp.size = 0.8f;
        rv.process (L.data() + pos, R.data() + pos, len, rp);
    }
    if (cpu) *cpu = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / secs * 100.0;
    return { L, R };
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "";
    int fails = 0;
    auto check = [&] (bool ok, const char* w) { std::printf ("%-50s %s\n", w, ok ? "ok" : "FAIL"); if (! ok) ++fails; };

    // 1. plant normalisation across growth / seeds
    double lo = 1e9, hi = 0;
    for (int seed = 1; seed < 40; ++seed)
        for (float g : { 0.0f, 0.3f, 1.0f })
        {
            PlantParams pp; pp.seed = seed; pp.growth = g; pp.branch = (seed % 5) / 4.0f; pp.bend = (seed % 3 - 1) * 0.7f;
            auto s = buildPlant (pp);
            double e = 0; for (auto& q : s.p) e += q.amp * q.amp;
            lo = std::min (lo, e); hi = std::max (hi, e);
        }
    check (lo > 0.99 && hi < 1.01, "plant spectra are energy-normalised");

    // 2. chord: level, finite, CPU
    PlantParams pp; pp.seed = 7; pp.growth = 0.7f; pp.branch = 0.5f; pp.leaves = 0.6f; pp.water = 0; (void) pp;
    VoiceParams vp; vp.bloomSec = 0.6f; vp.witherSec = 2.0f; vp.water = 0.25f; vp.wind = 0.5f; vp.touch = 0.3f;
    double cpu = 0;
    std::vector<Ev> chord;
    for (int k : { 48, 55, 60, 64, 67, 71, 74, 79, 83, 86 }) chord.push_back ({ 0.05, k, 0.8f, true });
    for (int k : { 48, 55, 60, 64, 67, 71, 74, 79, 83, 86 }) chord.push_back ({ 3.0, k, 0.0f, false });
    auto [L, R] = play (pp, vp, chord, 6.0, &cpu);
    float pk = 0; bool fin = true; double tail = 0;
    for (size_t i = 0; i < L.size(); ++i) { pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i]))); if (! std::isfinite (L[i])) fin = false; }
    for (size_t i = L.size() - 4800; i < L.size(); ++i) tail += std::abs (L[i]);
    std::printf ("  10-note chord: peak %.2f, cpu %.1f%% (incl. chorus+reverb), tail %.5f\n", pk, cpu, tail / 4800);
    check (fin && pk > 0.1f && pk < 1.5f, "10-voice chord level sane");
    check (cpu < 40.0, "CPU budget");

    // 3. release ends voices
    {
        Engine e; e.prepare (48000); e.setPlant (buildPlant (pp));
        std::vector<float> l (256), r (256);
        e.noteOn (60, 1.0f);
        VoiceParams q; q.witherSec = 0.2f;
        for (int i = 0; i < 100; ++i) e.render (l.data(), r.data(), 256, q);
        e.noteOff (60);
        for (int i = 0; i < 400; ++i) { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); e.render (l.data(), r.data(), 256, q); }
        check (e.activeVoices.load() == 0, "voices free after release");
    }

    if (! dir.empty())
    {
        wav (dir + "/garden_chord.wav", L, R, 48000);
        // a slow melodic phrase on a glassy, wind-blown plant
        PlantParams a; a.seed = 21; a.growth = 0.55f; a.branch = 0.3f; a.bend = 0.35f; a.leaves = 0.8f; a.roots = 0.2f; a.sun = 0.7f;
        VoiceParams av; av.bloomSec = 0.25f; av.witherSec = 1.8f; av.season = 0.2f; av.water = 0.15f; av.wind = 0.6f; av.touch = 0.4f; av.touchMaterial = stone::Glass;
        std::vector<Ev> mel;
        const int notes[] = { 76, 79, 83, 81, 79, 76, 74, 76 };
        for (int i = 0; i < 8; ++i) { mel.push_back ({ 0.1 + i * 0.6, notes[i], 0.8f, true }); mel.push_back ({ 0.1 + i * 0.6 + 0.5, notes[i], 0, false }); }
        auto [ml, mr] = play (a, av, mel, 7.5);
        wav (dir + "/garden_glass_melody.wav", ml, mr, 48000);
        // dark breathy autumn pad
        PlantParams b; b.seed = 3; b.growth = 0.9f; b.branch = 0.8f; b.bend = -0.3f; b.leaves = 0.2f; b.roots = 0.8f; b.sun = 0.3f;
        VoiceParams bv; bv.bloomSec = 1.5f; bv.witherSec = 4.0f; bv.season = 0.8f; bv.water = 0.6f; bv.wind = 0.4f; bv.touch = 0.0f;
        std::vector<Ev> pad = { { 0.1, 45, 0.8f, true }, { 0.1, 52, 0.7f, true }, { 0.1, 57, 0.7f, true }, { 0.1, 60, 0.6f, true },
                                { 4.0, 45, 0, false }, { 4.0, 52, 0, false }, { 4.0, 57, 0, false }, { 4.0, 60, 0, false } };
        auto [pl, pr] = play (b, bv, pad, 9.0);
        wav (dir + "/garden_autumn_pad.wav", pl, pr, 48000);
        // sprout: almost pure tone pluck
        PlantParams c; c.seed = 5; c.growth = 0.1f; c.roots = 0.3f;
        VoiceParams cv; cv.bloomSec = 0.005f; cv.witherSec = 0.6f; cv.touch = 0.6f; cv.wind = 0.2f; cv.season = 0.5f;
        std::vector<Ev> pl2;
        const int n2[] = { 60, 63, 67, 70, 72, 70, 67, 63 };
        for (int i = 0; i < 8; ++i) { pl2.push_back ({ 0.1 + i * 0.3, n2[i], 0.9f, true }); pl2.push_back ({ 0.1 + i * 0.3 + 0.15, n2[i], 0, false }); }
        auto [sl, sr2] = play (c, cv, pl2, 4.0);
        wav (dir + "/garden_sprout_pluck.wav", sl, sr2, 48000);
    }
    std::puts (fails ? "FAILED" : "ALL PASSED");
    return fails ? 1 : 0;
}
