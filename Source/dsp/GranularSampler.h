#pragma once

#include "DspUtils.h"
#include "GranularEngine.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>

namespace granula
{

// Decoded audio sample shared between the loader and the audio thread.
struct SampleData
{
    std::vector<float> left, right;
    double sampleRate = 44100.0;
    std::string name;
    std::vector<float> peaks; // min/max pairs for the waveform display

    int length() const { return (int) left.size(); }

    void buildPeaks (int numBins = 1200)
    {
        peaks.assign ((size_t) numBins * 2, 0.0f);
        const int n = length();
        if (n == 0) return;
        for (int b = 0; b < numBins; ++b)
        {
            const int s0 = (int) ((long long) b * n / numBins);
            const int s1 = std::max (s0 + 1, (int) ((long long) (b + 1) * n / numBins));
            float lo = 0.0f, hi = 0.0f;
            for (int i = s0; i < s1 && i < n; ++i)
            {
                const float v = 0.5f * (left[(size_t) i] + right[(size_t) i]);
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            peaks[(size_t) b * 2] = lo;
            peaks[(size_t) b * 2 + 1] = hi;
        }
    }
};

// Polyphonic granular sampler. Each MIDI note starts a voice that sprays
// grains from a loaded sample around a (possibly moving) play position.
class GranularSampler
{
public:
    static constexpr int kVoices = 8;
    static constexpr int kGrainsPerVoice = 48;
    static constexpr int kVis = GranularEngine::kMaxGrains;

    struct Params
    {
        float sizeMs = 120.0f;
        float intervalSamples = 4000.0f;
        bool synced = false;
        float position = 0.3f;   // 0..1 of the sample
        float spray = 0.05f;     // 0..1 of the sample (± around position)
        float scan = 0.0f;       // playhead speed, x realtime (-2..2)
        float pitchSemis = 0.0f;
        float pitchRandSemis = 0.0f;
        int scale = GranularEngine::Free;
        float detuneCents = 0.0f;
        float reverseProb = 0.0f;
        float spread = 0.5f;
        float shape = 0.3f;
        float attackMs = 10.0f;
        float releaseMs = 400.0f;
    };

    // UI snapshot
    std::array<GranularEngine::VisGrain, kVis> vis;
    std::array<std::atomic<float>, kVoices> voicePos {}; // <0 = inactive

    GranularSampler()
    {
        for (auto& v : voicePos) v.store (-1.0f);
    }

    void prepare (double sampleRate) { sr = sampleRate; reset(); }

    void reset()
    {
        for (auto& v : voices) { v.active = false; for (auto& g : v.grains) g.active = false; }
        for (auto& v : vis) v.amp.store (0.0f);
        for (auto& p : voicePos) p.store (-1.0f);
    }

    void noteOn (int note, float velocity)
    {
        // Retrigger the same note, else take a free voice, else steal the oldest.
        Voice* target = nullptr;
        for (auto& v : voices) if (v.active && v.note == note) { target = &v; break; }
        if (target == nullptr) for (auto& v : voices) if (! v.active) { target = &v; break; }
        if (target == nullptr)
        {
            target = &voices[0];
            for (auto& v : voices) if (v.startedAt < target->startedAt) target = &v;
        }

        auto& v = *target;
        const bool wasActive = v.active;
        v.active = true;
        v.note = note;
        v.velocity = velocity;
        v.released = false;
        v.startedAt = ++counter;
        v.scanOffset = 0.0;
        v.samplesToNext = 0.0;
        if (! wasActive) { v.env = 0.0f; for (auto& g : v.grains) g.active = false; }
    }

    void noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note) v.released = true;
    }

    void allNotesOff()
    {
        for (auto& v : voices) if (v.active) v.released = true;
    }

    // Adds the rendered grains to L/R (which the caller has cleared).
    void render (float* L, float* R, int n, const Params& p, const SampleData* s)
    {
        if (s == nullptr || s->length() < 64)
        {
            for (auto& v : voices) v.active = false;
            publish (nullptr, Params {});
            return;
        }

        const int len = s->length();
        const double srcRatio = s->sampleRate / sr;
        const float attackStep = 1.0f / std::max (1.0f, p.attackMs * 0.001f * (float) sr);
        const float releaseStep = 1.0f / std::max (1.0f, p.releaseMs * 0.001f * (float) sr);
        const float overlap = (float) (p.sizeMs * 0.001 * sr) / std::max (1.0f, p.intervalSamples);
        const float norm = 0.8f / std::sqrt (std::max (1.0f, overlap));
        const float* srcL = s->left.data();
        const float* srcR = s->right.data();

        for (auto& v : voices)
        {
            if (! v.active) continue;

            for (int i = 0; i < n; ++i)
            {
                // Envelope
                if (v.released)
                {
                    v.env -= releaseStep;
                    if (v.env <= 0.0f) { v.env = 0.0f; v.active = false; break; }
                }
                else if (v.env < 1.0f)
                    v.env = std::min (1.0f, v.env + attackStep);

                v.scanOffset += p.scan * srcRatio;

                v.samplesToNext -= 1.0;
                if (v.samplesToNext <= 0.0)
                {
                    spawn (v, p, len, srcRatio, norm);
                    const double jitter = p.synced ? 1.0 : (0.85 + 0.3 * rng.uni());
                    v.samplesToNext += std::max (1.0, (double) p.intervalSamples * jitter);
                }

                float outL = 0.0f, outR = 0.0f;
                for (auto& g : v.grains)
                {
                    if (! g.active) continue;
                    const float w = window ((float) g.age / (float) g.length, g.alpha) * g.gain;
                    const double pos = g.readPos;
                    if (pos >= 1.0 && pos < (double) len - 3.0)
                    {
                        const int i0 = (int) pos;
                        const float t = (float) (pos - i0);
                        const float sl = hermite (srcL[i0 - 1], srcL[i0], srcL[i0 + 1], srcL[i0 + 2], t);
                        const float sr2 = hermite (srcR[i0 - 1], srcR[i0], srcR[i0 + 1], srcR[i0 + 2], t);
                        outL += sl * w * g.panL;
                        outR += sr2 * w * g.panR;
                    }
                    g.readPos += g.inc;
                    if (++g.age >= g.length) g.active = false;
                }

                const float e = v.env * v.env; // softer curve
                L[i] += outL * e;
                R[i] += outR * e;
            }
        }

        publish (s, p);
    }

private:
    struct Grain
    {
        bool active = false;
        double readPos = 0, inc = 1;
        int length = 1, age = 0;
        float panL = 1, panR = 1, gain = 1, alpha = 1;
        float visPos = 0, visPitch = 0;
    };

    struct Voice
    {
        bool active = false, released = false;
        int note = 60;
        float velocity = 1.0f, env = 0.0f;
        double scanOffset = 0.0, samplesToNext = 0.0;
        uint64_t startedAt = 0;
        std::array<Grain, kGrainsPerVoice> grains {};
    };

    static inline float window (float t, float alpha)
    {
        const float a = alpha * 0.5f;
        if (t < a) return 0.5f * (1.0f - std::cos ((float) kPi * t / a));
        if (t > 1.0f - a) return 0.5f * (1.0f - std::cos ((float) kPi * (1.0f - t) / a));
        return 1.0f;
    }

    double playhead (const Voice& v, const Params& p, int len) const
    {
        double pos = std::fmod (p.position * (double) len + v.scanOffset, (double) len);
        if (pos < 0.0) pos += len;
        return pos;
    }

    float randomPitch (const Params& p) { return randomScalePitch (p.pitchRandSemis, p.scale, rng); }

    void spawn (Voice& v, const Params& p, int len, double srcRatio, float norm)
    {
        Grain* slot = nullptr;
        for (auto& g : v.grains) if (! g.active) { slot = &g; break; }
        if (slot == nullptr) return;

        const int length = std::max (16, (int) (p.sizeMs * 0.001 * sr));
        const float semis = std::clamp ((float) (v.note - 60) + p.pitchSemis + randomPitch (p)
                                            + rng.bi() * p.detuneCents * 0.01f, -48.0f, 48.0f);
        const double ratio = std::pow (2.0, semis / 12.0) * srcRatio;
        const bool reverse = rng.uni() < p.reverseProb;
        const double travel = ratio * length;

        double start = playhead (v, p, len) + rng.bi() * p.spray * (double) len;
        // Keep the whole grain inside the sample.
        const double lo = 2.0 + (reverse ? travel : 0.0);
        const double hi = (double) len - 4.0 - (reverse ? 0.0 : travel);
        if (hi <= lo) start = 0.5 * len; // grain longer than sample: just centre it
        else start = std::clamp (start, lo, hi);

        const float pan = rng.bi() * p.spread;
        const float angle = (pan + 1.0f) * (float) kPi * 0.25f;

        slot->active = true;
        slot->readPos = start;
        slot->inc = reverse ? -ratio : ratio;
        slot->length = length;
        slot->age = 0;
        slot->panL = std::cos (angle) * 1.41421356f;
        slot->panR = std::sin (angle) * 1.41421356f;
        slot->gain = norm * (0.25f + 0.75f * v.velocity);
        slot->alpha = 1.0f - 0.9f * std::clamp (p.shape, 0.0f, 1.0f);
        slot->visPos = (float) (start / len);
        slot->visPitch = semis;
    }

    void publish (const SampleData* s, const Params& p)
    {
        int k = 0;
        for (int vi = 0; vi < kVoices; ++vi)
        {
            const auto& v = voices[(size_t) vi];
            voicePos[(size_t) vi].store (v.active && s != nullptr ? (float) (playhead (v, p, s->length()) / s->length()) : -1.0f,
                                         std::memory_order_relaxed);
            if (! v.active) continue;
            for (const auto& g : v.grains)
            {
                if (! g.active || k >= kVis) continue;
                auto& vg = vis[(size_t) k++];
                vg.pos.store (g.visPos, std::memory_order_relaxed);
                vg.pitch.store (g.visPitch, std::memory_order_relaxed);
                vg.size.store (std::clamp (p.sizeMs / 1000.0f, 0.0f, 1.0f), std::memory_order_relaxed);
                vg.amp.store (window ((float) g.age / (float) g.length, g.alpha) * v.env, std::memory_order_relaxed);
            }
        }
        for (; k < kVis; ++k) vis[(size_t) k].amp.store (0.0f, std::memory_order_relaxed);
    }

    double sr = 44100.0;
    std::array<Voice, kVoices> voices {};
    uint64_t counter = 0;
    Rng rng { 0xC0FFEEu };
};

} // namespace granula
