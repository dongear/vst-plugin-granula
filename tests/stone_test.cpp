// STONE engine checks: every material x exciter makes sound with sane levels,
// trails produce the right number of hits, hold mode stops on note-off, no NaNs.
#include "../stone/Source/StoneDsp.h"
#include <cstdio>
#include <fstream>
#include <string>
using namespace stone;

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

static int lastHits = 0, hitsAtCut = -1;
static double cutAt = 1e9;
static std::pair<std::vector<float>, std::vector<float>> render (const EngineParams& p, const std::vector<Ev>& evs, double secs, int sr = 48000)
{
    Engine e; e.prepare (sr);
    const int n = (int) (secs * sr), B = 256;
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    size_t ei = 0;
    for (int pos = 0; pos < n; pos += B)
    {
        const int len = std::min (B, n - pos);
        while (ei < evs.size() && evs[ei].t * sr < pos + len)
        {
            e.setBlockOffset (std::max (0, (int) (evs[ei].t * sr) - pos));
            if (evs[ei].on) e.noteOn (evs[ei].note, evs[ei].vel, p); else e.noteOff (evs[ei].note);
            ++ei;
        }
        e.render (L.data() + pos, R.data() + pos, len, p);
        if (pos + len >= (int) (cutAt * sr) && hitsAtCut < 0) hitsAtCut = e.flashHead.load();
    }
    lastHits = e.flashHead.load();
    return { L, R };
}

static int countOnsets (const std::vector<float>& L, int sr)
{
    // energy envelope peaks separated by >= 8 ms
    int count = 0; float env = 0, prev = 0; int last = -100000;
    for (int i = 0; i < (int) L.size(); ++i)
    {
        const float a = std::abs (L[(size_t) i]);
        env = std::max (a, env * 0.995f);
        if (env > 0.01f && env > prev * 1.6f && i - last > sr / 125) { ++count; last = i; prev = env; }
        prev = std::max (prev * 0.9995f, 0.0f);
        if (env < prev) prev = env;
    }
    return count;
}

int main (int argc, char** argv)
{
    const std::string dir = argc > 1 ? argv[1] : "";
    const int sr = 48000;
    int fails = 0;
    auto check = [&] (bool ok, const std::string& what) { std::printf ("%-52s %s\n", what.c_str(), ok ? "ok" : "FAIL"); if (! ok) ++fails; };

    // 1. all materials x exciters
    float minPk = 10, maxPk = 0;
    for (int m = 0; m < NumMaterials; ++m)
        for (int x = 0; x < NumExciters; ++x)
            for (int note : { 36, 60, 84 })
            {
                EngineParams p; p.body.material = m; p.body.exciter = x;
                auto [L, R] = render (p, { { 0.01, note, 1.0f, true } }, 1.5);
                float pk = 0; bool fin = true;
                for (size_t i = 0; i < L.size(); ++i) { pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i]))); if (! std::isfinite (L[i])) fin = false; }
                minPk = std::min (minPk, pk); maxPk = std::max (maxPk, pk);
                if (! fin || pk < 0.02f || pk > 1.6f)
                {
                    std::printf ("  %s/%s note %d peak %.3f\n", material (m).name, exciterName (x), note, pk);
                    check (false, "material level");
                }
            }
    std::printf ("  peak range over materials/exciters/notes: %.3f .. %.3f\n", minPk, maxPk);
    check (minPk > 0.02f && maxPk < 1.6f, "all materials & exciters audible, no overload");

    // 2. trail counts
    {
        EngineParams p; p.hits = 8; p.intervalSec = 0.09; p.body.material = Wood; p.body.decay = 0.2f;
        auto [L, R] = render (p, { { 0.01, 60, 1.0f, true } }, 1.5);
        const int c = lastHits;
        std::printf ("  trail 8 hits -> %d strikes (onset detector saw %d)\n", c, countOnsets (L, sr));
        check (c >= 7 && c <= 9, "trail produces the requested hits");
        if (! dir.empty()) wav (dir + "/stone_trail_wood.wav", L, R, sr);
    }
    // 3. hold mode stops on note-off
    {
        EngineParams p; p.hold = true; p.intervalSec = 0.05; p.body.material = Stone; p.body.decay = 0.1f;
        auto [L, R] = render (p, { { 0.01, 60, 1.0f, true }, { 0.6, 60, 0.0f, false } }, 2.0);
        double late = 0; for (int i = (int) (1.2 * sr); i < (int) L.size(); ++i) late += std::abs (L[(size_t) i]);
        const int c = lastHits; // 0.6 s held at 50 ms -> ~12
        std::printf ("  hold: %d hits while held, residual after release %.4f\n", c, late / sr);
        check (c >= 10 && c <= 13 && late / sr < 0.001, "hold rolls while held and stops on release");
    }
    // 4. stress: dense accelerating trails, all mods random, many notes
    {
        EngineParams p; p.hits = 32; p.intervalSec = 0.03; p.accel = 1.0f; p.jitter = 1.0f; p.spread = 1.0f;
        p.body.material = Metal; p.body.decay = 1.0f; p.body.rough = 1.0f;
        p.mods[0] = { SrcRandom, DstPitch, 1.0f }; p.mods[1] = { SrcLfo, DstPan, 1.0f };
        p.mods[2] = { SrcTrail, DstBright, -1.0f }; p.mods[3] = { SrcStep, DstLevel, 1.0f };
        for (int i = 0; i < 8; ++i) p.steps[(size_t) i] = (float) (i % 3) / 2.0f;
        std::vector<Ev> evs;
        for (int k = 0; k < 12; ++k) evs.push_back ({ 0.01 + k * 0.05, 40 + k * 4, 1.0f, true });
        auto [L, R] = render (p, evs, 4.0);
        float pk = 0; bool fin = true;
        for (size_t i = 0; i < L.size(); ++i) { pk = std::max (pk, std::abs (L[i])); if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) fin = false; }
        std::printf ("  stress peak %.2f\n", pk);
        check (fin && pk < 3.0f, "stress: finite and bounded");
        if (! dir.empty()) wav (dir + "/stone_stress_metal.wav", L, R, sr);
    }
    // demos
    if (! dir.empty())
    {
        EngineParams p; p.hits = 12; p.intervalSec = 0.11; p.accel = 0.6f; p.jitter = 0.35f; p.hitDecay = 0.82f; p.spread = 0.8f;
        p.body.material = Glass; p.body.exciter = Mallet; p.body.hardness = 0.7f; p.body.decay = 0.55f; p.drift = 0.2f;
        p.mods[0] = { SrcRandom, DstPitch, 0.08f };
        std::vector<Ev> evs = { { 0.05, 72, 0.9f, true }, { 1.6, 79, 0.8f, true }, { 3.1, 76, 0.9f, true }, { 4.4, 84, 0.7f, true } };
        auto [L, R] = render (p, evs, 6.5);
        wav (dir + "/stone_glass_trails.wav", L, R, sr);

        EngineParams q; q.body.material = Water; q.body.exciter = Click; q.hits = 6; q.intervalSec = 0.13; q.jitter = 0.8f; q.spread = 1.0f;
        q.mods[0] = { SrcRandom, DstPitch, 0.35f };
        std::vector<Ev> e2; for (int k = 0; k < 6; ++k) e2.push_back ({ 0.05 + k * 0.7, 67 + (k * 5) % 12, 0.9f, true });
        auto [L2, R2] = render (q, e2, 5.0);
        wav (dir + "/stone_water_drops.wav", L2, R2, sr);

        EngineParams w; w.body.material = Stone; w.body.exciter = Noise; w.body.hardness = 0.4f; w.hits = 16; w.intervalSec = 0.05;
        w.accel = -0.5f; w.jitter = 0.5f; w.hitDecay = 0.9f; w.spread = 0.6f; w.body.rough = 0.6f;
        w.mods[0] = { SrcRandom, DstBright, 0.5f };
        auto [L3, R3] = render (w, { { 0.05, 43, 1.0f, true }, { 2.0, 48, 1.0f, true } }, 4.5);
        wav (dir + "/stone_pebbles.wav", L3, R3, sr);
    }

    std::puts (fails ? "FAILED" : "ALL PASSED");
    return fails ? 1 : 0;
}
