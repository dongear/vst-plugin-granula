// SOUND off + TUNE off must be transparent with zero latency; each switch works on its own.
#include "InterferenceProcessor.h"
#include <cstdio>
using namespace interference;

static void set (InterferenceProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto run = [&] (bool sound, bool tune, bool hq, const char* name)
    {
        InterferenceProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        set (p, ids::soundOn, sound ? 1.0f : 0.0f);
        set (p, ids::tuneOn, tune ? 1.0f : 0.0f);
        set (p, ids::quality, hq ? 1.0f : 0.0f);
        p.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        double maxDiff = 0, inE = 0, outE = 0;
        granula::Rng rng (5);
        for (int blk = 0; blk < 60; ++blk)
        {
            std::vector<float> ref (512);
            for (int i = 0; i < 512; ++i)
            {
                const float v = 0.3f * (float) std::sin (2.0 * kPi * 452.0 * (blk * 512 + i) / 48000.0) + 0.05f * rng.bi();
                ref[(size_t) i] = v; b.setSample (0, i, v); b.setSample (1, i, v);
            }
            p.processBlock (b, m);
            for (int i = 0; i < 512; ++i)
            {
                maxDiff = std::max (maxDiff, (double) std::abs (b.getSample (0, i) - ref[(size_t) i]));
                if (blk > 20) { inE += ref[(size_t) i] * ref[(size_t) i]; outE += b.getSample (0, i) * b.getSample (0, i); }
            }
        }
        const double lvl = 10.0 * std::log10 (outE / inE);
        std::printf ("%-34s latency %5d  max|out-in| %.5f  level %+.1f dB\n", name, p.getLatencySamples(), maxDiff, lvl);
        return std::make_pair (p.getLatencySamples(), maxDiff);
    };
    auto [l0, d0] = run (false, false, true, "SOUND off, TUNE off (HQ selected)");
    if (l0 != 0 || d0 > 1e-6) { std::puts ("  -> FAIL: should be transparent"); ++fails; }
    auto [l1, d1] = run (false, true, false, "TUNE only");
    if (l1 != 1728) { std::puts ("  -> FAIL: tune-only latency"); ++fails; }
    auto [l2, d2] = run (true, false, false, "SOUND only (zero latency)");
    if (l2 != 0 || d2 < 1e-3) { std::puts ("  -> FAIL: sound-only should process with 0 latency"); ++fails; }
    auto [l3, d3] = run (true, true, true, "SOUND (HQ) + TUNE");
    if (l3 != 4096 + 1728) { std::puts ("  -> FAIL: combined latency"); ++fails; }
    (void) d1; (void) d3;
    std::puts (fails ? "FAILED" : "ALL PASSED");
    return fails;
}
