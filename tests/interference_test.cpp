// Offline checks for the Interference DSP:
//  1. impulse through the spectral engine: correct latency and unity gain
//  2. in-key tone passes, out-of-key tone is attenuated (both engines)
//  3. SNAP moves an out-of-key tone into key (spectral engine)
//  4. colour > 100% leaves an in-key tail; no NaNs anywhere
#include "../interference/Source/HarmonicDsp.h"
#include <cstdio>

using namespace interference;

static double goertzel (const std::vector<float>& x, int start, int len, double f, double sr)
{
    const double w = 2.0 * kPi * f / sr, cw = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = start; i < start + len; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * (i - start) / len);
        const double s = x[(size_t) i] * win + cw * s1 - s2;
        s2 = s1; s1 = s;
    }
    const double p = s1 * s1 + s2 * s2 - cw * s1 * s2;
    return 2.0 * std::sqrt (std::max (0.0, p)) / (len * 0.5);
}

static double db (double v) { return 20.0 * std::log10 (std::max (1e-12, v)); }

template <typename Engine>
static std::vector<float> run (Engine& e, std::vector<float> x, const Params& p)
{
    std::vector<float> r = x;
    for (size_t pos = 0; pos < x.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, x.size() - pos);
        e.process (x.data() + pos, r.data() + pos, n, p);
    }
    return x;
}

int main()
{
    const double sr = 48000.0;
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { std::printf ("%-58s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++fails; };

    // 1. Impulse / latency
    {
        SpectralEngine s; s.prepare (sr);
        Params p; p.mask = 0xFFF; p.focus = 0.0f; p.lowHz = 20; p.highHz = 20000;
        std::vector<float> x (48000, 0.0f); x[1000] = 1.0f;
        auto y = run (s, x, p);
        int peakAt = 0; float peak = 0;
        for (int i = 0; i < (int) y.size(); ++i) if (std::abs (y[(size_t) i]) > peak) { peak = std::abs (y[(size_t) i]); peakAt = i; }
        double energy = 0; for (float v : y) energy += (double) v * v;
        std::printf ("  impulse: peak %.3f at %d (expected %d), energy %.3f\n", peak, peakAt, 1000 + s.getLatency(), energy);
        check (std::abs (peakAt - (1000 + s.getLatency())) <= 2, "spectral latency matches getLatency()");
    }

    // Test tones: A4 440 (in C major) and a quarter-tone-sharp A (452 Hz, out of key)
    const int len = 96000;
    auto tone = [&] (double f, double a)
    {
        std::vector<float> v ((size_t) len);
        for (int i = 0; i < len; ++i) v[(size_t) i] = (float) (a * std::sin (2.0 * kPi * f * i / sr));
        return v;
    };
    std::vector<float> mixd ((size_t) len);
    {
        auto a = tone (440.0, 0.3), b = tone (466.16, 0.3); // A (in key) + A# (out of C major)
        for (int i = 0; i < len; ++i) mixd[(size_t) i] = a[(size_t) i] + b[(size_t) i];
    }
    const int an = 16384, at = len - an - 100;

    Params p; p.mask = 0x0AB5; p.focus = 0.5f; p.color = 1.0f; p.lowHz = 40; p.highHz = 16000;

    // 2a. filter bank
    {
        FilterBank f; f.prepare (sr);
        auto y = run (f, mixd, p);
        const double inA = goertzel (mixd, at, an, 440, sr), inB = goertzel (mixd, at, an, 466.16, sr);
        const double oA = goertzel (y, at, an, 440, sr), oB = goertzel (y, at, an, 466.16, sr);
        std::printf ("  bank:     A %+.1f dB   A# %+.1f dB\n", db (oA / inA), db (oB / inB));
        check (std::abs (db (oA / inA)) < 3.0, "bank: in-key tone passes (within 3 dB)");
        check (db (oB / inB) < -12.0, "bank: out-of-key tone attenuated > 12 dB");
    }
    // 2b. spectral (mask)
    {
        SpectralEngine s; s.prepare (sr);
        auto y = run (s, mixd, p);
        const int L = s.getLatency();
        const double inA = goertzel (mixd, at - L, an, 440, sr), inB = goertzel (mixd, at - L, an, 466.16, sr); (void) L;
        const double oA = goertzel (y, at, an, 440, sr), oB = goertzel (y, at, an, 466.16, sr);
        std::printf ("  spectral: A %+.1f dB   A# %+.1f dB\n", db (oA / inA), db (oB / inB));
        check (std::abs (db (oA / inA)) < 2.0, "spectral: in-key tone passes (within 2 dB)");
        check (db (oB / inB) < -15.0, "spectral: out-of-key tone attenuated > 15 dB");
    }
    // 3. snap: 452 Hz should move to 440 Hz
    {
        auto x = tone (452.0, 0.4);
        SpectralEngine s; s.prepare (sr);
        Params q = p; q.snap = 1.0f;
        auto y = run (s, x, q);
        const double o440 = goertzel (y, at, an, 440, sr), o452 = goertzel (y, at, an, 452, sr);
        const double i452 = goertzel (x, at, an, 452, sr);
        std::printf ("  snap:     452->440: out@440 %+.1f dB, out@452 %+.1f dB (re input)\n", db (o440 / i452), db (o452 / i452));
        check (o440 > o452 * 4.0 && db (o440 / i452) > -4.0, "snap moves 452 Hz energy to 440 Hz");
    }
    // 4. tails + robustness on noise bursts
    for (int mode = 0; mode < 2; ++mode)
    {
        std::vector<float> x ((size_t) len, 0.0f);
        granula::Rng rng (7);
        for (int i = 0; i < 24000; ++i) x[(size_t) i] = 0.3f * rng.bi() + 0.3f * (float) std::sin (2.0 * kPi * 261.63 * i / sr);
        Params q = p; q.color = 2.0f; q.gate = 0.5f; q.snap = mode ? 0.6f : 0.0f;
        double tail = 0; bool finite = true; float peak = 0;
        std::vector<float> y;
        if (mode == 0) { FilterBank f; f.prepare (sr); y = run (f, x, q); }
        else { SpectralEngine s; s.prepare (sr); y = run (s, x, q); }
        for (int i = 0; i < len; ++i) { const float v = y[(size_t) i]; if (! std::isfinite (v)) finite = false; peak = std::max (peak, std::abs (v)); }
        for (int i = 40000; i < 52000; ++i) tail += (double) y[(size_t) i] * y[(size_t) i];
        tail = std::sqrt (tail / 12000.0);
        std::printf ("  %s colour 200%%: tail rms %.1f dBFS, peak %.1f dBFS\n", mode ? "spectral" : "bank    ", db (tail), db (peak));
        check (finite && peak < 4.0f, mode ? "spectral: stable with colour 200% + gate" : "bank: stable with colour 200% + gate");
        check (db (tail) > -70.0, mode ? "spectral: colour > 100% leaves a tail" : "bank: colour > 100% leaves a tail");
    }

    std::printf (fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
