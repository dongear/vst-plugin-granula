#pragma once

#include "../../Source/dsp/DspUtils.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

// STONE: modal percussion. An exciter (click / mallet / noise / scrape) strikes a
// bank of resonators whose frequency ratios come from a material. One MIDI note
// can unfold into a "trail" of strikes; every strike is its own voice with its
// own pitch, colour and stereo position.
namespace stone
{
using granula::kPi;
using granula::Rng;

constexpr int kModes = 12;
constexpr int kVoices = 32;

enum Material { Stone = 0, Glass, Wood, Metal, Ceramic, Skin, Water, Crystal, NumMaterials };
enum Exciter { Click = 0, Mallet, Noise, Scrape, NumExciters };

struct MaterialDef
{
    const char* name;
    float ratio[kModes];
    float gain[kModes];
    float decay;      // seconds, fundamental, at resonance = 50%
    float hfDamp;     // how much faster high modes die (0..1)
    float chirp;      // upward pitch glide at the start (water drops)
};

inline const MaterialDef& material (int m)
{
    static const MaterialDef defs[NumMaterials] = {
        { "Stone",   { 1.0f, 1.47f, 1.98f, 2.43f, 2.94f, 3.52f, 4.18f, 4.79f, 5.61f, 6.33f, 7.40f, 8.62f },
                     { 1.0f, 0.8f, 0.75f, 0.6f, 0.55f, 0.5f, 0.42f, 0.36f, 0.3f, 0.26f, 0.22f, 0.18f }, 0.35f, 0.8f, 0.0f },
        { "Glass",   { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f, 24.8f, 31.9f, 1.002f, 2.761f, 5.41f, 8.95f },
                     { 1.0f, 0.6f, 0.45f, 0.3f, 0.2f, 0.14f, 0.1f, 0.07f, 0.5f, 0.3f, 0.2f, 0.12f }, 2.4f, 0.35f, 0.0f },
        { "Wood",    { 1.0f, 3.93f, 9.24f, 16.3f, 2.4f, 5.9f, 1.5f, 12.1f, 20.7f, 7.3f, 4.6f, 25.0f },
                     { 1.0f, 0.35f, 0.15f, 0.08f, 0.12f, 0.08f, 0.1f, 0.05f, 0.03f, 0.04f, 0.05f, 0.02f }, 0.55f, 0.9f, 0.0f },
        { "Metal",   { 1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f, 3.16f, 3.50f, 3.60f, 4.06f, 4.15f, 4.60f },
                     { 1.0f, 0.9f, 0.8f, 0.75f, 0.7f, 0.62f, 0.58f, 0.5f, 0.48f, 0.42f, 0.4f, 0.35f }, 3.2f, 0.25f, 0.0f },
        { "Ceramic", { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f, 15.9f, 19.6f, 1.71f, 3.2f, 5.3f, 7.9f },
                     { 1.0f, 0.7f, 0.5f, 0.35f, 0.25f, 0.18f, 0.12f, 0.08f, 0.3f, 0.25f, 0.2f, 0.15f }, 1.0f, 0.55f, 0.0f },
        { "Skin",    { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.600f, 3.652f, 4.060f, 4.154f },
                     { 1.0f, 0.55f, 0.4f, 0.35f, 0.28f, 0.24f, 0.2f, 0.16f, 0.14f, 0.12f, 0.1f, 0.08f }, 0.32f, 0.95f, 0.0f },
        { "Water",   { 1.0f, 2.08f, 3.31f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
                     { 1.0f, 0.25f, 0.08f, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 0.12f, 0.6f, 0.9f },
        { "Crystal", { 1.0f, 2.0f, 3.0f, 4.02f, 5.05f, 6.1f, 7.18f, 8.3f, 0.5f, 1.5f, 2.51f, 9.4f },
                     { 1.0f, 0.55f, 0.4f, 0.3f, 0.22f, 0.16f, 0.12f, 0.09f, 0.25f, 0.2f, 0.15f, 0.06f }, 4.0f, 0.3f, 0.0f },
    };
    return defs[std::clamp (m, 0, NumMaterials - 1)];
}

inline const char* exciterName (int e)
{
    static const char* n[] = { "Click", "Mallet", "Noise", "Scrape" };
    return n[std::clamp (e, 0, NumExciters - 1)];
}

// Everything a single strike needs, already modulated.
struct Strike
{
    double freq = 220.0;     // fundamental Hz
    float level = 1.0f;      // linear
    float pan = 0.0f;        // -1..1
    float decay = 0.5f;      // resonance 0..1
    float bright = 0.6f;     // 0..1
    float damp = 0.5f;       // 0..1
    float hardness = 0.6f;   // 0..1
    float rough = 0.2f;      // 0..1
    float inharm = 0.0f;     // -1..1
    float click = 0.2f;      // direct exciter level
    int material = Stone;
    int exciter = Mallet;
};

class StrikeVoice
{
public:
    bool active = false;
    uint64_t age = 0;
    float energy = 0.0f;

    void start (const Strike& s, double sr, Rng& rng)
    {
        const auto& m = material (s.material);
        active = true;
        silentCount = 0;
        energy = 1.0f;
        sampleRate = sr;
        panL = std::cos ((s.pan + 1.0f) * (float) kPi * 0.25f) * 1.414f;
        panR = std::sin ((s.pan + 1.0f) * (float) kPi * 0.25f) * 1.414f;
        clickAmt = s.click;
        chirp = m.chirp;
        chirpPhase = 0.0f;

        const double decayBase = m.decay * std::pow (2.0, (s.decay - 0.5) * 6.0) * std::sqrt (220.0 / std::max (40.0, s.freq));
        const float stretch = 1.0f + 0.3f * s.inharm;
        numModes = 0;
        for (int k = 0; k < kModes; ++k)
        {
            if (m.gain[k] <= 0.0f) continue;
            const double jitterCents = s.rough * 40.0 * rng.bi();
            const double ratio = std::pow ((double) m.ratio[k], (double) stretch) * std::pow (2.0, jitterCents / 1200.0);
            const double f = s.freq * ratio;
            if (f > sr * 0.45 || f < 20.0) continue;
            auto& md = modes[(size_t) numModes++];
            md.baseW = 2.0 * kPi * f / sr;
            const double hf = std::pow (std::max (1.0, ratio), (double) (0.4 + 1.4 * s.damp * m.hfDamp));
            const double t60 = std::max (0.004, decayBase / hf);
            md.r = std::exp (-6.9078 / (t60 * sr));
            const float tilt = std::pow ((float) (k + 1), -(1.0f - s.bright) * 1.6f) * (0.4f + 0.6f * s.bright);
            md.gain = m.gain[k] * tilt;
            md.y1 = md.y2 = 0.0;
            setCoefs (md, md.baseW);
        }

        // exciter shape
        exciter = s.exciter;
        rough = s.rough;
        const float hard = std::clamp (s.hardness, 0.0f, 1.0f);
        switch (exciter)
        {
            case Click:  excLen = 2; break;
            case Mallet: excLen = std::max (2, (int) (sr * (0.004 - 0.0037 * hard))); break;
            case Noise:  excLen = std::max (8, (int) (sr * (0.03 - 0.027 * hard))); break;
            default:     excLen = (int) (sr * (0.12 - 0.06 * hard)); break;
        }
        excPos = 0;
        excLevel = s.level;
        // normalise the excitation so every exciter delivers similar energy
        switch (exciter)
        {
            case Click:  excNorm = 1.0f; break;
            case Mallet: excNorm = (float) kPi / (2.0f * (float) excLen); break;
            case Noise:  excNorm = 2.2f / std::sqrt ((float) excLen); break;
            default:     excNorm = 1.6f / std::sqrt ((float) excLen); break;
        }
        noiseLp = 0.0f;
        noiseCoef = 0.15f + 0.8f * hard;
        drive = 1.0f + 2.0f * s.rough;
    }

    // adds into L/R
    void render (float* L, float* R, int n, Rng& rng)
    {
        if (! active) return;
        for (int i = 0; i < n; ++i)
        {
            // excitation
            float x = 0.0f;
            if (excPos < excLen)
            {
                const float t = (float) excPos / (float) excLen;
                switch (exciter)
                {
                    case Click:  x = excPos == 0 ? 1.0f : -0.3f; break;
                    case Mallet: x = std::sin ((float) kPi * t) * 0.6f; break;
                    case Noise:
                        noiseLp += noiseCoef * (rng.bi() - noiseLp);
                        x = noiseLp * std::exp (-5.0f * t) * 0.9f;
                        break;
                    default:
                    {
                        noiseLp += noiseCoef * 0.5f * (rng.bi() - noiseLp);
                        const float env = std::sin ((float) kPi * t);
                        x = noiseLp * env * (0.6f + 0.4f * std::sin (t * 90.0f)) * 0.5f;
                    }
                }
                x += rough * 0.25f * rng.bi() * (1.0f - t);
                x *= excLevel;
                ++excPos;
            }
            const float raw = x;
            x *= excNorm;

            // water: quick upward glide
            if (chirp > 0.0f && (i & 15) == 0 && chirpPhase < 1.0f)
            {
                chirpPhase = std::min (1.0f, chirpPhase + 16.0f / (float) (0.035 * sampleRate));
                const double mult = 1.0 + chirp * (1.0 - std::pow (1.0 - chirpPhase, 2.0)) * 0.8;
                for (int k = 0; k < numModes; ++k) setCoefs (modes[(size_t) k], modes[(size_t) k].baseW * mult);
            }

            double y = 0.0;
            for (int k = 0; k < numModes; ++k)
            {
                auto& md = modes[(size_t) k];
                const double v = md.b0 * x + md.a1 * md.y1 - md.a2 * md.y2;
                md.y2 = md.y1; md.y1 = v;
                y += v * md.gain;
            }
            float out = (float) y + raw * clickAmt * 0.5f;
            out = std::tanh (out * drive) / drive * 1.9f;
            L[i] += out * panL;
            R[i] += out * panR;

            const float a = std::abs (out);
            energy = std::max (a, energy * 0.9995f);
        }
        if (excPos >= excLen && energy < 2.0e-5f)
        {
            if (++silentCount > 4) active = false;
        }
        else silentCount = 0;
    }

private:
    struct Mode { double baseW = 0, r = 0, a1 = 0, a2 = 0, b0 = 0, gain = 0, y1 = 0, y2 = 0; };

    void setCoefs (Mode& m, double w)
    {
        w = std::min (w, kPi * 0.9);
        m.a1 = 2.0 * m.r * std::cos (w);
        m.a2 = m.r * m.r;
        m.b0 = std::sin (w) * 0.5; // impulse response peak ~0.5, independent of pitch and decay
    }

    std::array<Mode, kModes> modes {};
    int numModes = 0, exciter = Mallet, excLen = 1, excPos = 0, silentCount = 0;
    float panL = 1, panR = 1, clickAmt = 0, excLevel = 1, excNorm = 1, noiseLp = 0, noiseCoef = 0.5f, rough = 0, drive = 1;
    float chirp = 0, chirpPhase = 1;
    double sampleRate = 48000.0;
};

// ---------------------------------------------------------------------------
// Modulation: 4 slots, each source -> destination * amount, evaluated per strike.
enum ModSource { SrcOff = 0, SrcRandom, SrcLfo, SrcTrail, SrcStep, SrcVelocity, NumSources };
enum ModDest { DstPitch = 0, DstDecay, DstBright, DstRough, DstPan, DstLevel, DstHardness, DstInharm, NumDests };

inline const char* sourceName (int s) { static const char* n[] = { "Off", "Random", "LFO", "Trail", "Step", "Velocity" }; return n[s]; }
inline const char* destName (int d) { static const char* n[] = { "Pitch", "Decay", "Bright", "Rough", "Pan", "Level", "Hardness", "Inharm" }; return n[d]; }

struct ModSlot { int src = SrcOff; int dst = DstPitch; float amount = 0.0f; };

// ---------------------------------------------------------------------------
struct EngineParams
{
    Strike body;                 // base strike (freq filled per note)
    float pitchSemis = 0.0f;     // body tuning relative to the note
    // trail
    int hits = 1;
    double intervalSec = 0.08;   // first gap
    float accel = 0.0f;          // -1 (slows) .. 1 (speeds up)
    float jitter = 0.0f;         // 0..1
    float density = 1.0f;        // probability a hit sounds
    float hitDecay = 0.8f;       // level multiplier per hit
    float drift = 0.0f;          // semitones per hit
    float spread = 0.5f;         // random stereo spread
    bool hold = false;           // keep rolling while the key is held
    // mods
    std::array<ModSlot, 4> mods {};
    float lfoRate = 1.0f;        // Hz
    int lfoShape = 0;            // 0 sine 1 tri 2 square 3 sample&hold
    std::array<float, 8> steps {};
};

class Engine
{
public:
    // for the UI
    struct Flash { std::atomic<float> pan { 0.0f }, level { 0.0f }, time { -1.0f }; };
    std::array<Flash, 32> flashes;
    std::atomic<int> flashHead { 0 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& v : voices) v.active = false;
        pending.clear();
        pending.reserve (512);
        lfoPhase = 0.0;
        clock = 0;
    }

    void reset()
    {
        for (auto& v : voices) v.active = false;
        pending.clear();
    }

    void noteOn (int note, float vel, const EngineParams& p)
    {
        // schedule the trail
        const int total = std::max (1, p.hold ? 512 : p.hits);
        double t = (double) clock + (double) curOffset;
        double gap = p.intervalSec * sr;
        for (int k = 0; k < total && (int) pending.size() < 1024; ++k)
        {
            Pending e;
            e.time = (int64_t) std::llround (t);
            e.note = note;
            e.vel = vel;
            e.index = k;
            e.total = p.hold ? std::max (1, p.hits) : total;
            e.hold = p.hold;
            pending.push_back (e);
            const double jit = 1.0 + p.jitter * 0.9 * rng.bi();
            t += std::max (sr * 0.004, gap * jit);
            gap *= std::pow (2.0, -0.25 * p.accel);
            gap = std::clamp (gap, sr * 0.006, sr * 4.0);
        }
    }

    void noteOff (int note)
    {
        // in hold mode the roll stops when the key is released
        pending.erase (std::remove_if (pending.begin(), pending.end(),
                                       [note] (const Pending& e) { return e.hold && e.note == note && e.index > 0; }),
                       pending.end());
    }

    void allOff() { pending.clear(); }

    // Renders n samples; `offsetEvents` are note events inside this block handled by caller.
    void render (float* L, float* R, int n, const EngineParams& p)
    {
        int pos = 0;
        while (pos < n)
        {
            // next due strike within this block
            int64_t nextT = INT64_MAX;
            for (auto& e : pending) nextT = std::min (nextT, e.time);
            const int64_t blockEnd = clock + n;
            const int segEnd = nextT < blockEnd ? (int) std::max<int64_t> (pos, nextT - clock) : n;
            if (segEnd > pos)
            {
                for (auto& v : voices) v.render (L + pos, R + pos, segEnd - pos, rng);
                pos = segEnd;
            }
            if (nextT < blockEnd && nextT - clock <= pos)
            {
                // fire every strike due now
                for (size_t i = 0; i < pending.size();)
                {
                    if (pending[i].time - clock <= pos) { fire (pending[i], p); pending[i] = pending.back(); pending.pop_back(); }
                    else ++i;
                }
            }
        }
        lfoPhase += p.lfoRate * n / sr;
        lfoPhase -= std::floor (lfoPhase);
        clock += n;
    }

    void setBlockOffset (int o) { curOffset = o; }

private:
    struct Pending { int64_t time = 0; int note = 60; float vel = 1; int index = 0; int total = 1; bool hold = false; };

    float lfoValue (const EngineParams& p)
    {
        const double ph = lfoPhase;
        switch (p.lfoShape)
        {
            case 1: return (float) (ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph);
            case 2: return ph < 0.5 ? 1.0f : -1.0f;
            case 3:
                if (ph < lastLfoPhase) shValue = rng.bi();
                lastLfoPhase = ph;
                return shValue;
            default: return (float) std::sin (2.0 * kPi * ph);
        }
    }

    void fire (const Pending& e, const EngineParams& p)
    {
        if (e.index > 0 && rng.uni() > p.density) return;

        Strike s = p.body;
        const float trailPos = e.total > 1 ? (float) std::min (e.index, e.total - 1) / (float) (e.total - 1) : 0.0f;
        double semis = (e.note - 60) + p.pitchSemis + p.drift * (float) e.index;
        float level = e.vel * std::pow (p.hitDecay, (float) e.index);
        float pan = p.spread * rng.bi();

        float lfo = lfoValue (p);
        for (const auto& m : p.mods)
        {
            if (m.src == SrcOff || std::abs (m.amount) < 1.0e-4f) continue;
            float v = 0.0f;
            switch (m.src)
            {
                case SrcRandom:   v = rng.bi(); break;
                case SrcLfo:      v = lfo; break;
                case SrcTrail:    v = trailPos * 2.0f - 1.0f; break;
                case SrcStep:     v = p.steps[(size_t) (e.index % 8)] * 2.0f - 1.0f; break;
                case SrcVelocity: v = e.vel * 2.0f - 1.0f; break;
                default: break;
            }
            const float a = v * m.amount;
            switch (m.dst)
            {
                case DstPitch:    semis += a * 12.0; break;
                case DstDecay:    s.decay = std::clamp (s.decay + a * 0.5f, 0.0f, 1.0f); break;
                case DstBright:   s.bright = std::clamp (s.bright + a * 0.5f, 0.0f, 1.0f); break;
                case DstRough:    s.rough = std::clamp (s.rough + a * 0.5f, 0.0f, 1.0f); break;
                case DstPan:      pan += a; break;
                case DstLevel:    level *= granula::dbToGain (a * 12.0f); break;
                case DstHardness: s.hardness = std::clamp (s.hardness + a * 0.5f, 0.0f, 1.0f); break;
                case DstInharm:   s.inharm = std::clamp (s.inharm + a, -1.0f, 1.0f); break;
                default: break;
            }
        }
        s.freq = 440.0 * std::pow (2.0, (semis + 60.0 - 69.0) / 12.0);
        s.level = std::clamp (level, 0.0f, 2.0f);
        s.pan = std::clamp (pan, -1.0f, 1.0f);

        // pick a free voice, else the quietest
        StrikeVoice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr)
        {
            v = &voices[0];
            for (auto& c : voices) if (c.energy < v->energy) v = &c;
        }
        v->start (s, sr, rng);
        v->age = ++ageCounter;

        const int h = flashHead.load() % (int) flashes.size();
        flashes[(size_t) h].pan.store (s.pan);
        flashes[(size_t) h].level.store (s.level);
        flashes[(size_t) h].time.store ((float) (clock / sr));
        flashHead.store (h + 1);
    }

public:
    double now() const { return clock / sr; }

private:
    double sr = 48000.0;
    std::array<StrikeVoice, kVoices> voices {};
    std::vector<Pending> pending;
    Rng rng { 0x51A7E5u };
    int64_t clock = 0;
    int curOffset = 0;
    uint64_t ageCounter = 0;
    double lfoPhase = 0.0, lastLfoPhase = 0.0;
    float shValue = 0.0f;
};

} // namespace stone
