#pragma once

#include "../../Source/dsp/DspUtils.h"
#include "../../stone/Source/StoneDsp.h"
#include <array>
#include <atomic>
#include <cstdint>

// GARDEN: an organic, playable additive synth. A seeded "plant" defines the
// sound: each leaf is a partial. Growth adds leaves, branches shape the
// spectrum into organic formant clusters, bend makes it bell-like, wind keeps
// every partial slightly alive, the season decides how notes bloom and wither.
namespace garden
{
using granula::kPi;
using granula::Rng;

constexpr int kPartials = 32;
constexpr int kVoices = 10;

struct PlantParams
{
    int seed = 1;
    float growth = 0.5f;  // 0 sprout .. 1 full bush
    float branch = 0.4f;  // spectral clusters
    float bend = 0.0f;    // -1..1 inharmonicity
    float leaves = 0.4f;  // upper shimmer
    float roots = 0.4f;   // low body / sub
    float sun = 0.5f;     // brightness
    float width = 0.6f;   // stereo spread of partials
    float water = 0.0f;   // (unused here, kept for symmetry)
};

struct Partial
{
    float ratio = 1, amp = 0, pan = 0;
    float drift = 0;      // random cents offset (static)
    float wobbleRate = 1; // Hz
    float wobblePhase = 0;
    float stagger = 0;    // 0..1 order in which leaves open
    float height = 0;     // 0..1 position on the plant (UI)
    int branchId = 0;
};

struct PlantSpec
{
    std::array<Partial, kPartials> p {};
    float subAmp = 0;
    int branches = 1;
};

inline float smooth01 (float x) { x = std::clamp (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

inline PlantSpec buildPlant (const PlantParams& pp)
{
    PlantSpec s;
    Rng rng ((uint32_t) pp.seed * 2654435761u + 12345u);
    s.branches = 1 + (int) std::lround (pp.branch * 5.0f);

    // formant-like clusters: one per branch, at seed-chosen harmonic positions
    std::array<float, 6> centre {}, bw {}, gain {};
    for (int b = 0; b < s.branches; ++b)
    {
        centre[(size_t) b] = 1.0f + rng.uni() * 18.0f;
        bw[(size_t) b] = 1.5f + rng.uni() * 5.0f;
        gain[(size_t) b] = 0.4f + rng.uni() * 0.6f;
    }

    const float grown = 1.0f + pp.growth * (kPartials - 1);
    double sumSq = 0.0;
    for (int k = 0; k < kPartials; ++k)
    {
        auto& q = s.p[(size_t) k];
        const float n = (float) (k + 1);
        // bend: stretched (bell / stone) or compressed (soft, hollow) series
        const float stretch = pp.bend >= 0 ? 1.0f + pp.bend * 0.012f * std::pow (n, 1.35f)
                                           : 1.0f / (1.0f - pp.bend * 0.006f * std::pow (n, 1.2f));
        q.drift = rng.bi() * 4.0f;
        q.ratio = n * stretch;

        // amplitude: tilt by sun, leaves lift the top, roots the bottom
        float a = std::pow (n, -(1.0f - pp.sun) * 1.9f - 0.25f);
        if (k >= 6) a *= 0.5f + 2.2f * pp.leaves * smooth01 ((n - 6.0f) / 10.0f);
        if (k < 3) a *= 1.0f + pp.roots * (k == 0 ? 1.2f : 0.6f);

        // branch clusters (organic colour), mixed with a plain series
        float cl = 0.0f;
        for (int b = 0; b < s.branches; ++b)
        {
            const float d = (n - centre[(size_t) b]) / bw[(size_t) b];
            cl += gain[(size_t) b] * std::exp (-0.5f * d * d);
        }
        a *= (1.0f - pp.branch) + pp.branch * (0.15f + 1.6f * cl);
        a *= 0.75f + 0.5f * rng.uni(); // each leaf a bit different

        // growth: leaves appear one after another
        a *= smooth01 (grown - (float) k);
        q.amp = a;
        sumSq += (double) a * a;

        q.pan = std::clamp ((k % 2 == 0 ? -1.0f : 1.0f) * (0.3f + 0.7f * rng.uni()) * pp.width * smooth01 (n / 4.0f), -1.0f, 1.0f);
        q.wobbleRate = 0.15f + 1.6f * rng.uni();
        q.wobblePhase = rng.uni();
        q.stagger = std::clamp ((float) k / (float) kPartials * 0.8f + 0.2f * rng.uni(), 0.0f, 1.0f);
        q.height = std::clamp ((float) k / (float) (kPartials - 1), 0.0f, 1.0f);
        q.branchId = s.branches > 1 ? (int) (rng.next() % (uint32_t) s.branches) : 0;
    }
    const float norm = sumSq > 0 ? (float) (1.0 / std::sqrt (sumSq)) : 0.0f;
    for (auto& q : s.p) q.amp *= norm;
    s.subAmp = pp.roots * 0.55f;
    return s;
}

struct VoiceParams
{
    float bloomSec = 0.05f;   // attack
    float witherSec = 0.8f;   // release
    float season = 0.3f;      // 0 spring .. 1 autumn
    float water = 0.2f;       // breath
    float wind = 0.3f;        // movement
    float touch = 0.2f;       // pluck at note-on
    int touchMaterial = stone::Wood;
    float octave = 0.0f;      // semitones offset (octaves * 12)
    float bendSemis = 0.0f;   // pitch wheel
};

// fast sine via table
struct SineTable
{
    static constexpr int N = 4096;
    std::array<float, N + 1> t {};
    SineTable() { for (int i = 0; i <= N; ++i) t[(size_t) i] = (float) std::sin (2.0 * kPi * i / N); }
    inline float operator() (double phase) const // phase 0..1
    {
        const double x = phase * N;
        const int i = (int) x;
        const float f = (float) (x - i);
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * f;
    }
};
inline const SineTable& sineTable() { static const SineTable s; return s; }

class Voice
{
public:
    bool active = false, released = false;
    int note = 60;
    uint64_t age = 0;
    float level = 0.0f; // envelope (for stealing / UI)
    std::array<float, kPartials> leafLevel {};

    void start (int n, float vel, double sr, Rng& rng)
    {
        note = n; velocity = vel; active = true; released = false;
        sampleRate = sr;
        t = 0.0; relT = 0.0; relStart = 0.0f;
        for (auto& ph : phase) ph = rng.uni();
        subPhase = 0.0;
        for (auto& r : breath) { r.y1 = r.y2 = 0.0; }
        hiss = 0.0f;
        touchPending = true;
    }

    void release() { if (! released) { released = true; relT = 0.0; relStart = env; } }

    // renders n samples (n <= 64) and adds to L/R
    void render (float* L, float* R, int n, const PlantSpec& plant, const VoiceParams& vp, Rng& rng)
    {
        if (! active) return;
        const double f0 = 440.0 * std::pow (2.0, (note - 69 + vp.octave + vp.bendSemis) / 12.0);
        const double dt = (double) n / sampleRate;
        const float season = vp.season;

        // ---- envelope (control rate)
        const float bloom = std::max (0.002f, vp.bloomSec), wither = std::max (0.01f, vp.witherSec);
        float envNext;
        if (! released)
        {
            const float a = (float) std::min (1.0, (t + dt) / bloom);
            envNext = a * a * (3.0f - 2.0f * a);
            // autumn: the sustain slowly wilts
            const float wilt = (float) std::exp (-(t + dt) * season * 0.35);
            envNext *= 0.35f + 0.65f * wilt;
        }
        else
        {
            const float r = (float) std::exp (-(relT + dt) * 4.6 / wither);
            envNext = relStart * r;
            relT += dt;
        }
        const float envNow = env;
        env = envNext;
        level = env;

        // per-partial targets
        const float windDepth = vp.wind;
        for (int k = 0; k < kPartials; ++k)
        {
            const auto& q = plant.p[(size_t) k];
            if (q.amp <= 1.0e-5f) { tgt[(size_t) k] = 0.0f; continue; }
            // leaves open one after another during the bloom (more spread in spring)
            const float stag = q.stagger * (0.3f + 0.7f * (1.0f - season)) * bloom * 1.5f;
            const float open = released ? 1.0f : smooth01 ((float) ((t - stag) / (bloom * 0.6 + 0.002)));
            // high leaves fade earlier in autumn, live longer in spring
            const float age01 = (float) (t / (0.4 + 6.0 * (1.0 - season)));
            const float fade = std::exp (-age01 * q.height * (0.2f + 2.5f * season));
            // wind: slow amplitude breathing per leaf
            const float wob = 1.0f + windDepth * 0.35f * (float) std::sin (2.0 * kPi * (q.wobblePhase + q.wobbleRate * t));
            tgt[(size_t) k] = q.amp * open * fade * wob;
        }

        const float vel = 0.35f + 0.65f * velocity;
        const auto& sine = sineTable();
        const double inc0 = f0 / sampleRate;
        const double nyq = 0.45;

        // per-partial increments (control rate): drift + wind wander
        std::array<double, kPartials> inc {};
        for (int k = 0; k < kPartials; ++k)
        {
            const auto& q = plant.p[(size_t) k];
            const double cents = q.drift * (0.5 + windDepth) + windDepth * 6.0 * std::sin (2.0 * kPi * (q.wobblePhase * 3.1 + q.wobbleRate * 0.37 * t));
            inc[(size_t) k] = inc0 * q.ratio * std::pow (2.0, cents / 1200.0);
        }

        for (int i = 0; i < n; ++i)
        {
            const float frac = (float) (i + 1) / (float) n;
            const float e = (envNow + (env - envNow) * frac) * vel;
            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < kPartials; ++k)
            {
                const float a = cur[(size_t) k] + (tgt[(size_t) k] - cur[(size_t) k]) * frac;
                if (a <= 1.0e-6f) continue;
                const auto& q = plant.p[(size_t) k];
                const double ik = inc[(size_t) k];
                if (ik >= nyq) continue;
                double& ph = phase[(size_t) k];
                ph += ik; if (ph >= 1.0) ph -= 1.0;
                const float s = sine (ph) * a * e;
                l += s * (1.0f - q.pan) * 0.5f;
                r += s * (1.0f + q.pan) * 0.5f;
            }
            // roots: sub one octave down
            if (plant.subAmp > 0.0f)
            {
                subPhase += inc0 * 0.5; if (subPhase >= 1.0) subPhase -= 1.0;
                const float sb = sine (subPhase) * plant.subAmp * e;
                l += sb * 0.5f; r += sb * 0.5f;
            }
            // water: breath through resonators on the first partials + air
            if (vp.water > 0.001f)
            {
                const float nz = rng.bi();
                hiss += 0.6f * (nz - hiss);
                float b = 0.0f;
                for (int k = 0; k < 4; ++k) b += (float) breath[(size_t) k].process (nz);
                const float br = (b * 0.5f + (nz - hiss) * 0.08f) * vp.water * e;
                l += br; r += br * 0.9f;
            }
            L[i] += l * 0.15f;
            R[i] += r * 0.15f;
        }
        cur = tgt;
        t += dt;
        for (int k = 0; k < kPartials; ++k) leafLevel[(size_t) k] = cur[(size_t) k] * env;

        // tune breath resonators to this note
        if (vp.water > 0.001f)
            for (int k = 0; k < 4; ++k)
                breath[(size_t) k].tune (f0 * plant.p[(size_t) k].ratio, sampleRate, 0.9993 - 0.0004 * k);

        if (released && env < 1.0e-4f) active = false;
    }

    bool takeTouch() { const bool tp = touchPending; touchPending = false; return tp; }
    float getVelocity() const { return velocity; }

private:
    struct Reso
    {
        double a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0;
        void tune (double f, double sr, double r)
        {
            const double w = 2.0 * kPi * std::min (f, sr * 0.45) / sr;
            a1 = 2.0 * r * std::cos (w); a2 = r * r; g = (1.0 - r) * 4.0;
        }
        inline double process (double x) { const double y = g * x + a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; }
    };

    double sampleRate = 48000.0, t = 0.0, relT = 0.0;
    float velocity = 1.0f, env = 0.0f, relStart = 0.0f, hiss = 0.0f;
    std::array<double, kPartials> phase {};
    std::array<float, kPartials> cur {}, tgt {};
    double subPhase = 0.0;
    std::array<Reso, 4> breath {};
    bool touchPending = false;
};

class Engine
{
public:
    std::array<std::atomic<float>, kPartials> leafGlow {}; // UI
    std::atomic<int> activeVoices { 0 };

    void prepare (double s)
    {
        sr = s;
        for (auto& v : voices) v.active = false;
        for (auto& tv : touches) tv.active = false;
    }
    void reset() { prepare (sr); }

    void setPlant (const PlantSpec& p) { plant = p; }

    void noteOn (int note, float vel)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (c.active && c.note == note) { v = &c; break; }
        if (! v) for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (! v)
        {
            v = &voices[0];
            for (auto& c : voices)
                if ((c.released && ! v->released) || (c.released == v->released && c.age < v->age)) v = &c;
        }
        v->start (note, vel, sr, rng);
        v->age = ++ageCounter;
    }

    void noteOff (int note)
    {
        for (auto& c : voices)
            if (c.active && c.note == note && ! c.released)
            {
                if (sustain) held[(size_t) note] = true; else c.release();
            }
    }

    void setSustain (bool on)
    {
        sustain = on;
        if (! on)
            for (int nn = 0; nn < 128; ++nn)
                if (held[(size_t) nn])
                {
                    held[(size_t) nn] = false;
                    for (auto& c : voices) if (c.active && c.note == nn && ! c.released) c.release();
                }
    }

    void allOff() { for (auto& c : voices) c.release(); }

    void render (float* L, float* R, int n, const VoiceParams& vp)
    {
        for (int pos = 0; pos < n; pos += 32)
        {
            const int len = std::min (32, n - pos);
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.takeTouch() && vp.touch > 0.001f) startTouch (v, vp);
                v.render (L + pos, R + pos, len, plant, vp, rng);
            }
            for (auto& tv : touches) tv.render (L + pos, R + pos, len, rng);
        }
        // UI read-outs
        std::array<float, kPartials> glow {};
        int count = 0;
        for (auto& v : voices)
            if (v.active)
            {
                ++count;
                for (int k = 0; k < kPartials; ++k) glow[(size_t) k] = std::max (glow[(size_t) k], v.leafLevel[(size_t) k]);
            }
        for (int k = 0; k < kPartials; ++k) leafGlow[(size_t) k].store (glow[(size_t) k], std::memory_order_relaxed);
        activeVoices.store (count, std::memory_order_relaxed);
    }

private:
    void startTouch (const Voice& v, const VoiceParams& vp)
    {
        stone::StrikeVoice* tv = nullptr;
        for (auto& c : touches) if (! c.active) { tv = &c; break; }
        if (! tv) tv = &touches[0];
        stone::Strike s;
        s.material = vp.touchMaterial;
        s.exciter = stone::Mallet;
        s.freq = 440.0 * std::pow (2.0, (v.note - 69 + vp.octave + vp.bendSemis) / 12.0) * 2.0;
        s.level = vp.touch * (0.4f + 0.6f * v.getVelocity()) * 0.6f;
        s.decay = 0.25f;
        s.bright = 0.55f;
        s.hardness = 0.55f;
        s.rough = 0.1f;
        s.click = 0.05f;
        s.pan = 0.0f;
        tv->start (s, sr, rng);
    }

    double sr = 48000.0;
    std::array<Voice, kVoices> voices {};
    std::array<stone::StrikeVoice, 8> touches {};
    PlantSpec plant;
    Rng rng { 0x6A7DE7u };
    uint64_t ageCounter = 0;
    bool sustain = false;
    std::array<bool, 128> held {};
};

// Stereo chorus for width / shimmer.
class Chorus
{
public:
    void prepare (double s) { sr = s; size = (int) (0.05 * sr); for (auto& b : buf) b.assign ((size_t) size, 0.0f); pos = 0; ph = 0; }
    void process (float* L, float* R, int n, float mix)
    {
        if (mix < 0.001f) return;
        for (int i = 0; i < n; ++i)
        {
            buf[0][(size_t) pos] = L[i]; buf[1][(size_t) pos] = R[i];
            ph += 0.23 / sr; if (ph >= 1.0) ph -= 1.0;
            const double m0 = (0.011 + 0.004 * std::sin (2.0 * kPi * ph)) * sr;
            const double m1 = (0.013 + 0.004 * std::sin (2.0 * kPi * (ph + 0.33))) * sr;
            const float wl = granula::readCircular (buf[0].data(), size, (double) pos - m0);
            const float wr = granula::readCircular (buf[1].data(), size, (double) pos - m1);
            if (++pos >= size) pos = 0;
            L[i] = L[i] * (1.0f - 0.5f * mix) + wr * mix * 0.7f;
            R[i] = R[i] * (1.0f - 0.5f * mix) + wl * mix * 0.7f;
        }
    }
private:
    double sr = 48000.0, ph = 0.0;
    int size = 1, pos = 0;
    std::vector<float> buf[2];
};

} // namespace garden
