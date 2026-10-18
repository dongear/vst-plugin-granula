#pragma once

#include "DspUtils.h"

namespace granula
{

// Stereo / ping-pong delay with a filtered, gently saturated feedback path.
class StereoDelay
{
public:
    struct Params
    {
        bool enabled = true;
        float timeMs = 375.0f;   // already resolved from tempo sync if needed
        float feedback = 0.4f;   // 0..0.95
        float toneHz = 6000.0f;  // low-pass in the feedback loop
        bool pingPong = false;
        float mix = 0.3f;        // 0..1
    };

    static constexpr float kMaxTimeMs = 2500.0f;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = (int) std::ceil (kMaxTimeMs * 0.001 * sr) + 16;
        lineL.assign ((size_t) size, 0.0f);
        lineR.assign ((size_t) size, 0.0f);
        timeSm.prepare (sr, 120.0f);
        timeSm.snap (375.0f * 0.001f * (float) sr);
        mixSm.prepare (sr, 20.0f);
        mixSm.snap (0.0f);
        lastTone = -1.0f;
        reset();
    }

    void reset()
    {
        std::fill (lineL.begin(), lineL.end(), 0.0f);
        std::fill (lineR.begin(), lineR.end(), 0.0f);
        lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset();
        writePos = 0;
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        if (std::abs (p.toneHz - lastTone) > 1.0e-3f)
        {
            lastTone = p.toneHz;
            lpL.setLowpass (sr, p.toneHz, 0.6); lpR.setLowpass (sr, p.toneHz, 0.6);
            hpL.setHighpass (sr, 90.0, 0.6);    hpR.setHighpass (sr, 90.0, 0.6);
        }

        timeSm.target = std::clamp (p.timeMs, 1.0f, kMaxTimeMs) * 0.001f * (float) sr;
        mixSm.target = p.enabled ? p.mix : 0.0f;
        const float fb = std::clamp (p.feedback, 0.0f, 0.95f);

        // When disabled the wet level fades out smoothly (no clicks on toggle).
        for (int i = 0; i < n; ++i)
        {
            const float d = timeSm.next();
            const float mix = mixSm.next();
            const double rp = (double) writePos - (double) d;
            const float yL = readCircular (lineL.data(), size, rp);
            const float yR = readCircular (lineR.data(), size, rp);

            const float fbL = std::tanh (hpL.process (lpL.process (yL)) * fb * 1.1f) / 1.1f;
            const float fbR = std::tanh (hpR.process (lpR.process (yR)) * fb * 1.1f) / 1.1f;

            const float inL = p.enabled ? L[i] : 0.0f;
            const float inR = p.enabled ? R[i] : 0.0f;

            if (p.pingPong)
            {
                lineL[(size_t) writePos] = 0.5f * (inL + inR) + fbR;
                lineR[(size_t) writePos] = fbL;
            }
            else
            {
                lineL[(size_t) writePos] = inL + fbL;
                lineR[(size_t) writePos] = inR + fbR;
            }
            if (++writePos >= size) writePos = 0;

            float dryG, wetG;
            wetDryGains (mix, dryG, wetG);
            L[i] = L[i] * dryG + yL * wetG;
            R[i] = R[i] * dryG + yR * wetG;
        }
    }

private:
    double sr = 44100.0;
    int size = 1, writePos = 0;
    std::vector<float> lineL, lineR;
    Biquad lpL, lpR, hpL, hpR;
    Smoother timeSm, mixSm;
    float lastTone = -1.0f;
};

} // namespace granula
