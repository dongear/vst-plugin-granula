#pragma once

#include "DspUtils.h"
#include <array>
#include <atomic>

namespace granula
{

// Real-time granular processor working on the live input.
// The input is continuously recorded into a circular buffer; grains are short,
// windowed snippets read back from that buffer with their own pitch,
// direction, start offset and stereo position.
class GranularEngine
{
public:
    static constexpr int kMaxGrains = 96;
    static constexpr float kMaxPositionMs = 2000.0f;
    static constexpr float kMaxSprayMs = 1000.0f;

    enum Scale { Free = 0, Chromatic, Major, Minor, Pentatonic, Fifths, Octaves, NumScales };

    struct Params
    {
        float sizeMs = 120.0f;
        float intervalSamples = 4410.0f; // time between grain onsets
        bool synced = false;             // no timing jitter when tempo-synced
        float positionMs = 80.0f;        // how far behind "now" grains start
        float sprayMs = 40.0f;           // random extra offset
        float pitchSemis = 0.0f;
        float pitchRandSemis = 0.0f;
        int scale = Free;
        float detuneCents = 0.0f;
        float reverseProb = 0.0f;        // 0..1
        float spread = 0.5f;             // 0..1
        float shape = 0.5f;              // 0 = smooth (Hann) .. 1 = flat/hard
        float feedback = 0.0f;           // 0..0.95
        bool freeze = false;
        float mix = 0.5f;                // 0..1
    };

    // Lock-free snapshot of grains for the UI.
    struct VisGrain
    {
        std::atomic<float> pos { 0.0f };   // 0..1 across (position + spray) range
        std::atomic<float> pitch { 0.0f }; // semitones
        std::atomic<float> size { 0.0f };  // 0..1
        std::atomic<float> amp { 0.0f };   // current envelope, 0 = inactive
    };
    std::array<VisGrain, kMaxGrains> vis;
    std::atomic<float> inputLevel { 0.0f };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        bufSize = (int) std::ceil (sr * 9.0); // 9 s: room for pos + spray + 4x pitched 1 s grains
        bufL.assign ((size_t) bufSize, 0.0f);
        bufR.assign ((size_t) bufSize, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        writePos = 0;
        samplesToNext = 0.0;
        lastWetL = lastWetR = 0.0f;
        for (auto& g : grains) g.active = false;
        for (auto& v : vis) v.amp.store (0.0f);
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        const float dryG = std::cos (p.mix * (float) kPi * 0.5f);
        const float wetG = std::sin (p.mix * (float) kPi * 0.5f);
        const float overlap = (float) (p.sizeMs * 0.001 * sr) / std::max (1.0f, p.intervalSamples);
        const float norm = 1.0f / std::sqrt (std::max (1.0f, overlap));
        float peak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = R[i];
            peak = std::max (peak, std::max (std::abs (inL), std::abs (inR)));

            if (! p.freeze)
            {
                bufL[(size_t) writePos] = inL + p.feedback * std::tanh (lastWetL);
                bufR[(size_t) writePos] = inR + p.feedback * std::tanh (lastWetR);
                if (++writePos >= bufSize) writePos = 0;
            }

            samplesToNext -= 1.0;
            if (samplesToNext <= 0.0)
            {
                spawnGrain (p, norm);
                const double jitter = p.synced ? 1.0 : (0.85 + 0.3 * rng.uni());
                samplesToNext += std::max (1.0, (double) p.intervalSamples * jitter);
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float env = window ((float) g.age / (float) g.length, g.alpha) * g.gain;
                wetL += readCircular (bufL.data(), bufSize, g.readPos) * env * g.panL;
                wetR += readCircular (bufR.data(), bufSize, g.readPos) * env * g.panR;
                g.readPos += g.inc;
                if (g.readPos < 0.0) g.readPos += bufSize;
                else if (g.readPos >= bufSize) g.readPos -= bufSize;
                if (++g.age >= g.length) g.active = false;
            }

            lastWetL = wetL;
            lastWetR = wetR;
            L[i] = inL * dryG + wetL * wetG;
            R[i] = inR * dryG + wetR * wetG;
        }

        // Publish grain state for the UI.
        for (int k = 0; k < kMaxGrains; ++k)
        {
            const auto& g = grains[(size_t) k];
            vis[(size_t) k].amp.store (g.active ? window ((float) g.age / (float) g.length, g.alpha) : 0.0f,
                                       std::memory_order_relaxed);
        }
        inputLevel.store (peak, std::memory_order_relaxed);
    }

private:
    struct Grain
    {
        bool active = false;
        double readPos = 0, inc = 1;
        int length = 1, age = 0;
        float panL = 1, panR = 1, gain = 1, alpha = 1;
    };

    // Tukey window: alpha = 1 → Hann, alpha → 0 → rectangular.
    static inline float window (float t, float alpha)
    {
        const float a = alpha * 0.5f;
        if (t < a) return 0.5f * (1.0f - std::cos ((float) kPi * t / a));
        if (t > 1.0f - a) return 0.5f * (1.0f - std::cos ((float) kPi * (1.0f - t) / a));
        return 1.0f;
    }

    float randomPitch (const Params& p)
    {
        if (p.pitchRandSemis <= 0.0f) return 0.0f;
        const float r = rng.bi() * p.pitchRandSemis;
        if (p.scale == Free) return r;

        static const int chromatic[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
        static const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
        static const int penta[] = { 0, 2, 4, 7, 9 };
        static const int fifths[] = { 0, 7 };
        static const int octaves[] = { 0 };

        const int* deg = chromatic; int nDeg = 12;
        switch (p.scale)
        {
            case Major:      deg = major;   nDeg = 7; break;
            case Minor:      deg = minor;   nDeg = 7; break;
            case Pentatonic: deg = penta;   nDeg = 5; break;
            case Fifths:     deg = fifths;  nDeg = 2; break;
            case Octaves:    deg = octaves; nDeg = 1; break;
            default: break;
        }

        // Nearest allowed degree within the random range.
        float best = 0.0f, bestDist = 1e9f;
        const int maxSemi = (int) std::ceil (p.pitchRandSemis);
        for (int oct = -3; oct <= 3; ++oct)
            for (int d = 0; d < nDeg; ++d)
            {
                const int s = oct * 12 + deg[d];
                if (std::abs (s) > maxSemi) continue;
                const float dist = std::abs ((float) s - r);
                if (dist < bestDist) { bestDist = dist; best = (float) s; }
            }
        return best;
    }

    void spawnGrain (const Params& p, float norm)
    {
        Grain* slot = nullptr;
        int slotIndex = 0;
        for (int k = 0; k < kMaxGrains; ++k)
            if (! grains[(size_t) k].active) { slot = &grains[(size_t) k]; slotIndex = k; break; }
        if (slot == nullptr) return; // all voices busy, skip this onset

        const int length = std::max (16, (int) (p.sizeMs * 0.001 * sr));
        const float semis = std::clamp (p.pitchSemis + randomPitch (p) + rng.bi() * p.detuneCents * 0.01f, -36.0f, 36.0f);
        const double ratio = std::pow (2.0, semis / 12.0);
        const bool reverse = rng.uni() < p.reverseProb;

        double offset = (p.positionMs + rng.uni() * p.sprayMs) * 0.001 * sr;

        // Keep the read head behind the write head and inside valid history.
        double minOff = 4.0, maxOff;
        if (! reverse)
            minOff += p.freeze ? ratio * length : std::max (0.0, (ratio - 1.0) * length);
        maxOff = bufSize - 8.0 - (reverse ? ratio * length : 0.0) - (p.freeze ? 0.0 : (double) length);
        offset = std::clamp (offset, minOff, std::max (minOff, maxOff));

        double start = (double) writePos - offset;
        while (start < 0.0) start += bufSize;

        const float pan = rng.bi() * p.spread;
        const float angle = (pan + 1.0f) * (float) kPi * 0.25f;

        slot->active = true;
        slot->readPos = start;
        slot->inc = reverse ? -ratio : ratio;
        slot->length = length;
        slot->age = 0;
        slot->panL = std::cos (angle) * 1.41421356f;
        slot->panR = std::sin (angle) * 1.41421356f;
        slot->gain = norm;
        slot->alpha = 1.0f - 0.9f * std::clamp (p.shape, 0.0f, 1.0f);

        auto& v = vis[(size_t) slotIndex];
        v.pos.store ((float) std::clamp (offset / ((kMaxPositionMs + kMaxSprayMs) * 0.001 * sr), 0.0, 1.0), std::memory_order_relaxed);
        v.pitch.store (semis, std::memory_order_relaxed);
        v.size.store (std::clamp (p.sizeMs / 1000.0f, 0.0f, 1.0f), std::memory_order_relaxed);
    }

    double sr = 44100.0;
    int bufSize = 1;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    double samplesToNext = 0.0;
    float lastWetL = 0, lastWetR = 0;
    std::array<Grain, kMaxGrains> grains {};
    Rng rng { 0x1234567u };
};

} // namespace granula
