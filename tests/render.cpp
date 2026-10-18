// Offline render test for the Granula DSP chain (no JUCE, no host).
// Usage: granula_render <outDir>
// Renders a synthetic test phrase through several settings, writes WAV files
// and prints level / sanity stats. Exits non-zero on NaN/Inf or runaway levels.

#include "dsp/GranularEngine.h"
#include "dsp/GranularSampler.h"
#include "dsp/TapeSaturation.h"
#include "dsp/StereoDelay.h"
#include "dsp/FdnReverb.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

using namespace granula;

static void writeWav (const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr)
{
    std::ofstream f (path, std::ios::binary);
    const uint32_t n = (uint32_t) L.size();
    const uint32_t dataBytes = n * 2 * 2;
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) std::lround (std::clamp (L[i], -1.0f, 1.0f) * 32767.0f);
        int16_t b = (int16_t) std::lround (std::clamp (R[i], -1.0f, 1.0f) * 32767.0f);
        f.write ((const char*) &a, 2); f.write ((const char*) &b, 2);
    }
}

// A little plucked arpeggio with a soft noise "breath" — enough material for grains.
static void makeInput (std::vector<float>& L, std::vector<float>& R, int sr, double seconds)
{
    const int n = (int) (sr * seconds);
    L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
    const double notes[] = { 57, 60, 64, 67, 69, 67, 64, 60 }; // A minor-ish
    const double noteLen = 0.375;
    Rng rng (42);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const int idx = (int) (t / noteLen);
        if (t > seconds * 0.55) break; // leave a tail so we can hear decay
        const double nt = t - idx * noteLen;
        const double f = 440.0 * std::pow (2.0, (notes[idx % 8] - 69.0) / 12.0);
        const double env = std::exp (-nt * 5.0) * std::min (1.0, nt * 400.0);
        double s = 0.0;
        for (int h = 1; h <= 6; ++h) s += std::sin (2.0 * kPi * f * h * t) / (h * h);
        s *= env * 0.35;
        s += rng.bi() * 0.01 * env;
        L[(size_t) i] = (float) s;
        R[(size_t) i] = (float) (s * 0.9);
    }
}

struct Setting
{
    std::string name;
    GranularEngine::Params g;
    TapeSaturation::Params t;
    StereoDelay::Params d;
    FdnReverb::Params r;
};

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : ".";
    const int sr = 48000;
    const int block = 256;
    std::vector<float> inL, inR;
    makeInput (inL, inR, sr, 10.0);

    std::vector<Setting> settings;
    {
        Setting s; s.name = "01_dry_bypass";
        s.g.mix = 0.0f; s.t.enabled = false; s.d.enabled = false; s.r.enabled = false;
        settings.push_back (s);
    }
    {
        Setting s; s.name = "02_init";
        s.g.sizeMs = 120; s.g.intervalSamples = sr / 12.0f; s.g.positionMs = 80; s.g.sprayMs = 40; s.g.detuneCents = 6;
        s.g.spread = 0.6f; s.g.shape = 0.3f; s.g.mix = 0.6f;
        s.t.driveDb = 6; s.t.wow = 0.25f; s.t.flutter = 0.15f; s.t.age = 0.25f;
        s.d.timeMs = 375; s.d.feedback = 0.4f; s.d.toneHz = 5000; s.d.pingPong = true; s.d.mix = 0.25f;
        s.r.size = 0.6f; s.r.decaySec = 3.5f; s.r.damping = 0.45f; s.r.preDelayMs = 20; s.r.mix = 0.3f;
        settings.push_back (s);
    }
    {
        Setting s = settings[1]; s.name = "03_cloud_pad";
        s.g.sizeMs = 420; s.g.intervalSamples = sr / 24.0f; s.g.positionMs = 300; s.g.sprayMs = 450;
        s.g.pitchRandSemis = 12; s.g.scale = GranularEngine::Octaves; s.g.detuneCents = 10; s.g.reverseProb = 0.3f;
        s.g.spread = 1.0f; s.g.shape = 0.05f; s.g.feedback = 0.3f; s.g.mix = 0.85f;
        s.r.size = 0.9f; s.r.decaySec = 8; s.r.mix = 0.5f; s.d.mix = 0.18f;
        settings.push_back (s);
    }
    {
        Setting s = settings[1]; s.name = "04_shimmer_up12";
        s.g.sizeMs = 260; s.g.intervalSamples = sr / 20.0f; s.g.positionMs = 120; s.g.sprayMs = 200;
        s.g.pitchSemis = 12; s.g.pitchRandSemis = 12; s.g.scale = GranularEngine::Octaves; s.g.feedback = 0.45f; s.g.mix = 0.6f;
        s.r.decaySec = 10; s.r.mix = 0.45f;
        settings.push_back (s);
    }
    {
        Setting s = settings[1]; s.name = "05_glitch_reverse";
        s.g.sizeMs = 45; s.g.synced = true; s.g.intervalSamples = (float) (sr * 60.0 / 120.0 * 0.25);
        s.g.positionMs = 0; s.g.sprayMs = 120; s.g.pitchRandSemis = 7; s.g.scale = GranularEngine::Fifths;
        s.g.reverseProb = 0.5f; s.g.shape = 0.9f; s.g.mix = 0.75f; s.t.driveDb = 12; s.d.enabled = false; s.r.mix = 0.15f;
        settings.push_back (s);
    }
    {
        Setting s = settings[1]; s.name = "06_lofi_cassette";
        s.g.mix = 0.3f; s.t.driveDb = 14; s.t.wow = 0.7f; s.t.flutter = 0.5f; s.t.age = 0.7f; s.t.hiss = 0.35f;
        s.d.mix = 0.2f; s.r.mix = 0.2f;
        settings.push_back (s);
    }
    {
        Setting s = settings[1]; s.name = "07_stress_max";
        s.g.sizeMs = 1000; s.g.intervalSamples = sr / 100.0f; s.g.positionMs = 2000; s.g.sprayMs = 1000;
        s.g.pitchSemis = 24; s.g.pitchRandSemis = 24; s.g.reverseProb = 0.5f; s.g.feedback = 0.95f; s.g.mix = 1.0f;
        s.t.driveDb = 24; s.t.wow = 1; s.t.flutter = 1; s.t.age = 1; s.t.hiss = 1;
        s.d.timeMs = 2000; s.d.feedback = 0.95f; s.d.toneHz = 20000; s.d.mix = 1.0f;
        s.r.size = 1; s.r.decaySec = 20; s.r.damping = 0; s.r.mix = 1.0f;
        settings.push_back (s);
    }
    {
        Setting s = settings[2]; s.name = "08_freeze_after_3s";
        settings.push_back (s); // freeze toggled during render below
    }

    int failures = 0;
    for (const auto& st : settings)
    {
        GranularEngine gr; TapeSaturation tp; StereoDelay dl; FdnReverb rv;
        gr.prepare (sr); tp.prepare (sr); dl.prepare (sr); rv.prepare (sr);

        std::vector<float> L = inL, R = inR;
        const int n = (int) L.size();
        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            auto g = st.g;
            if (st.name == "08_freeze_after_3s" && pos > sr * 3) g.freeze = true;
            gr.process (L.data() + pos, R.data() + pos, len, g);
            tp.process (L.data() + pos, R.data() + pos, len, st.t);
            dl.process (L.data() + pos, R.data() + pos, len, st.d);
            rv.process (L.data() + pos, R.data() + pos, len, st.r);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

        double sumSq = 0, tailSq = 0; float peak = 0; bool bad = false;
        for (int i = 0; i < n; ++i)
        {
            for (float v : { L[(size_t) i], R[(size_t) i] })
            {
                if (! std::isfinite (v)) bad = true;
                peak = std::max (peak, std::abs (v));
                sumSq += (double) v * v;
                if (i > n * 9 / 10) tailSq += (double) v * v;
            }
        }
        const double rms = std::sqrt (sumSq / (2.0 * n));
        const double tailRms = std::sqrt (tailSq / (2.0 * (n / 10)));
        const double cpu = secs / (n / (double) sr) * 100.0;
        const bool fail = bad || peak > 8.0f;
        failures += fail ? 1 : 0;

        std::printf ("%-22s peak %6.2f dBFS  rms %6.1f dBFS  tail %6.1f dBFS  cpu %5.2f%%  %s\n", st.name.c_str(),
                     20.0 * std::log10 (std::max (1e-9f, peak)), 20.0 * std::log10 (std::max (1e-12, rms)),
                     20.0 * std::log10 (std::max (1e-12, tailRms)), cpu, fail ? "FAIL" : "ok");
        writeWav (outDir + "/" + st.name + ".wav", L, R, sr);
    }
    writeWav (outDir + "/00_input.wav", inL, inR, sr);

    // ---- Sampler (instrument): the test phrase as a sample, played with notes
    struct SamplerCase { std::string name; GranularSampler::Params p; };
    std::vector<SamplerCase> cases;
    { SamplerCase c; c.name = "s1_pad_chord"; c.p.sizeMs = 380; c.p.intervalSamples = sr / 26.0f; c.p.position = 0.3f;
      c.p.spray = 0.08f; c.p.scan = 0.05f; c.p.pitchRandSemis = 12; c.p.scale = GranularEngine::Octaves; c.p.detuneCents = 9;
      c.p.spread = 1.0f; c.p.shape = 0.05f; c.p.attackMs = 900; c.p.releaseMs = 2500; cases.push_back (c); }
    { SamplerCase c; c.name = "s2_timestretch"; c.p.sizeMs = 90; c.p.intervalSamples = sr / 45.0f; c.p.position = 0.0f;
      c.p.spray = 0.002f; c.p.scan = 0.5f; c.p.attackMs = 5; c.p.releaseMs = 300; cases.push_back (c); }
    { SamplerCase c; c.name = "s3_stress"; c.p.sizeMs = 1000; c.p.intervalSamples = sr / 100.0f; c.p.position = 0.9f;
      c.p.spray = 1.0f; c.p.scan = -2.0f; c.p.pitchSemis = 24; c.p.pitchRandSemis = 24; c.p.reverseProb = 0.5f;
      c.p.attackMs = 1; c.p.releaseMs = 10; cases.push_back (c); }

    SampleData sample;
    sample.left = inL; sample.right = inR; sample.sampleRate = 44100.0; // deliberately different rate
    sample.buildPeaks();

    for (const auto& c : cases)
    {
        GranularSampler smp; TapeSaturation tp; StereoDelay dl; FdnReverb rv;
        smp.prepare (sr); tp.prepare (sr); dl.prepare (sr); rv.prepare (sr);
        TapeSaturation::Params tpp; StereoDelay::Params dpp; dpp.mix = 0.2f; FdnReverb::Params rpp; rpp.mix = 0.35f;
        const int n = sr * 10;
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
        const auto t0 = std::chrono::steady_clock::now();
        for (int pos = 0; pos < n; pos += block)
        {
            const int len = std::min (block, n - pos);
            if (pos == 0) { smp.noteOn (48, 0.9f); smp.noteOn (55, 0.8f); smp.noteOn (60, 0.8f); smp.noteOn (64, 0.7f); }
            if (c.name == "s3_stress" && pos == sr * 2) for (int k = 0; k < 12; ++k) smp.noteOn (40 + k * 3, 1.0f); // voice stealing
            if (pos >= sr * 6 && pos < sr * 6 + block) smp.allNotesOff();
            smp.render (L.data() + pos, R.data() + pos, len, c.p, &sample);
            tp.process (L.data() + pos, R.data() + pos, len, tpp);
            dl.process (L.data() + pos, R.data() + pos, len, dpp);
            rv.process (L.data() + pos, R.data() + pos, len, rpp);
        }
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        float peak = 0; double sumSq = 0; bool bad = false;
        for (int i = 0; i < n; ++i) for (float v : { L[(size_t) i], R[(size_t) i] })
        { if (! std::isfinite (v)) bad = true; peak = std::max (peak, std::abs (v)); sumSq += (double) v * v; }
        const bool fail = bad || peak > 8.0f || peak < 0.01f;
        failures += fail ? 1 : 0;
        std::printf ("%-22s peak %6.2f dBFS  rms %6.1f dBFS  cpu %5.2f%%  %s\n", c.name.c_str(),
                     20.0 * std::log10 (std::max (1e-9f, peak)), 10.0 * std::log10 (std::max (1e-24, sumSq / (2.0 * n))),
                     secs / 10.0 * 100.0, fail ? "FAIL" : "ok");
        writeWav (outDir + "/" + c.name + ".wav", L, R, sr);
    }
    return failures == 0 ? 0 : 1;
}
