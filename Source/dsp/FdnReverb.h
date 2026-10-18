#pragma once

#include "DspUtils.h"
#include <array>

namespace granula
{

// 8-line feedback delay network reverb with input diffusion, damping,
// gentle delay modulation and pre-delay.
class FdnReverb
{
public:
    struct Params
    {
        bool enabled = true;
        float size = 0.6f;        // 0..1
        float decaySec = 3.0f;    // RT60
        float damping = 0.4f;     // 0..1
        float preDelayMs = 20.0f; // 0..250
        float width = 1.0f;       // 0..1
        float mix = 0.3f;         // 0..1
    };

    static constexpr int N = 8;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lineSize = (int) std::ceil (0.25 * sr) + 16;
        for (auto& l : lines) l.assign ((size_t) lineSize, 0.0f);
        preSize = (int) std::ceil (0.3 * sr) + 16;
        preL.assign ((size_t) preSize, 0.0f);
        preR.assign ((size_t) preSize, 0.0f);

        static const float apMsL[kAp] = { 4.71f, 3.59f, 2.41f, 1.73f };
        static const float apMsR[kAp] = { 5.13f, 3.89f, 2.67f, 1.53f };
        for (int k = 0; k < kAp; ++k)
        {
            apL[k].init ((int) (apMsL[k] * 0.001 * sr));
            apR[k].init ((int) (apMsR[k] * 0.001 * sr));
        }

        sizeSm.prepare (sr, 250.0f);
        sizeSm.snap (0.6f);
        mixSm.prepare (sr, 20.0f);
        mixSm.snap (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& l : lines) std::fill (l.begin(), l.end(), 0.0f);
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        for (auto& a : apL) a.clear();
        for (auto& a : apR) a.clear();
        damp.fill (0.0f);
        writePos = prePos = 0;
        lfoPhase = 0.0;
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        static const float baseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f };

        sizeSm.target = p.size;
        mixSm.target = p.enabled ? p.mix : 0.0f;
        const float dampCoeff = 1.0f - 0.85f * std::clamp (p.damping, 0.0f, 1.0f); // one-pole LP amount
        const int preSamples = std::clamp ((int) (p.preDelayMs * 0.001 * sr), 0, preSize - 2);
        const float decay = std::max (0.1f, p.decaySec);
        const float modDepth = (float) (0.0012 * sr);
        const float inGain = p.enabled ? 0.5f : 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float sizeNow = sizeSm.next();
            const float scale = 0.35f + 1.25f * sizeNow;
            const float mix = mixSm.next();

            // Pre-delay
            preL[(size_t) prePos] = L[i] * inGain;
            preR[(size_t) prePos] = R[i] * inGain;
            int rp = prePos - preSamples; if (rp < 0) rp += preSize;
            float xL = preL[(size_t) rp], xR = preR[(size_t) rp];
            if (++prePos >= preSize) prePos = 0;

            // Input diffusion
            for (int k = 0; k < kAp; ++k) { xL = apL[k].process (xL); xR = apR[k].process (xR); }

            lfoPhase += 0.37 / sr;
            if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            const float lfo = (float) std::sin (2.0 * kPi * lfoPhase);

            std::array<float, N> y {};
            float sum = 0.0f;
            for (int k = 0; k < N; ++k)
            {
                float len = baseMs[k] * scale * 0.001f * (float) sr;
                if (k == 1) len += modDepth * lfo;
                if (k == 6) len -= modDepth * lfo;
                const float out = readCircular (lines[(size_t) k].data(), lineSize, (double) writePos - (double) len);
                damp[(size_t) k] += dampCoeff * (out - damp[(size_t) k]);
                const float g = std::pow (10.0f, -3.0f * len / (decay * (float) sr));
                y[(size_t) k] = damp[(size_t) k] * g;
                sum += y[(size_t) k];
            }

            // Householder feedback matrix: y - 2/N * sum(y)
            const float h = sum * (2.0f / N);
            for (int k = 0; k < N; ++k)
            {
                const float in = (k & 1) ? xR : xL;
                lines[(size_t) k][(size_t) writePos] = y[(size_t) k] - h + in;
            }
            if (++writePos >= lineSize) writePos = 0;

            float outL = (y[0] - y[2] + y[4] - y[6]) * 0.6f;
            float outR = (y[1] - y[3] + y[5] - y[7]) * 0.6f;

            // Width (mid/side)
            const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * p.width;
            outL = mid + side; outR = mid - side;

            float dryG, wetG;
            wetDryGains (mix, dryG, wetG);
            L[i] = L[i] * dryG + outL * wetG;
            R[i] = R[i] * dryG + outR * wetG;
        }
    }

private:
    struct Allpass
    {
        std::vector<float> buf; int pos = 0; float g = 0.6f;
        void init (int len) { buf.assign ((size_t) std::max (1, len), 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline float process (float x)
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            if (++pos >= (int) buf.size()) pos = 0;
            return d - g * v;
        }
    };

    static constexpr int kAp = 4;
    double sr = 44100.0;
    int lineSize = 1, writePos = 0, preSize = 1, prePos = 0;
    std::array<std::vector<float>, N> lines;
    std::vector<float> preL, preR;
    Allpass apL[kAp], apR[kAp];
    std::array<float, N> damp {};
    double lfoPhase = 0.0;
    Smoother sizeSm, mixSm;
};

} // namespace granula
