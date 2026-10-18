#pragma once

#include "DspUtils.h"

namespace granula
{

// Cassette-style processor: emphasis EQ around an asymmetric soft clipper,
// wow & flutter pitch wobble, "age" high-frequency loss and tape hiss.
// The wobble delay line is always running (even when bypassed) so the plugin
// latency stays constant.
class TapeSaturation
{
public:
    struct Params
    {
        bool enabled = true;
        float driveDb = 6.0f; // 0..24
        float wow = 0.2f;     // 0..1
        float flutter = 0.1f; // 0..1
        float age = 0.2f;     // 0..1
        float hiss = 0.0f;    // 0..1
    };

    static constexpr float kBaseDelayMs = 2.5f;

    int getLatencySamples() const { return baseDelay; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        baseDelay = (int) std::round (kBaseDelayMs * 0.001 * sr);
        lineSize = baseDelay * 2 + 64;
        for (auto& ch : chans)
        {
            ch.line.assign ((size_t) lineSize, 0.0f);
            ch.pre.setHighShelf (sr, 3000.0, 6.0);
            ch.de.setHighShelf (sr, 3000.0, -6.0);
            ch.dc.setHighpass (sr, 20.0);
            ch.hissHp.setHighpass (sr, 1800.0);
        }
        driveSm.prepare (sr, 30.0f);
        driveSm.snap (1.0f);
        lastAge = -1.0f;
        reset();
    }

    void reset()
    {
        for (auto& ch : chans)
        {
            std::fill (ch.line.begin(), ch.line.end(), 0.0f);
            ch.pre.reset(); ch.de.reset(); ch.dc.reset(); ch.lp1.reset(); ch.lp2.reset(); ch.hissHp.reset();
            ch.hp.reset();
        }
        writePos = 0;
        wowPhase = flutterPhase = 0.0;
        drift = driftTarget = 0.0f;
        driftCounter = 0;
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        if (std::abs (p.age - lastAge) > 1.0e-6f)
        {
            lastAge = p.age;
            const double lpHz = 20000.0 * std::pow (4500.0 / 20000.0, (double) p.age);
            const double hpHz = 15.0 + 75.0 * p.age;
            for (auto& ch : chans)
            {
                ch.lp1.setLowpass (sr, lpHz, 0.6);
                ch.lp2.setLowpass (sr, lpHz * 1.15, 0.75);
                ch.hp.setHighpass (sr, hpHz, 0.6);
            }
        }

        driveSm.target = dbToGain (p.driveDb);
        const double wowRate = 0.55, flutterRate = 7.3;
        const float wowDepth = p.wow * 1.6f * 0.001f * (float) sr;        // samples
        const float flutterDepth = p.flutter * 0.12f * 0.001f * (float) sr;
        const float hissLevel = p.hiss * p.hiss * 0.02f;
        const float bias = 0.18f;
        const float biasOut = std::tanh (bias);
        float* io[2] = { L, R };

        for (int i = 0; i < n; ++i)
        {
            // Slow random drift makes the wow feel less mechanical.
            if (--driftCounter <= 0)
            {
                driftCounter = (int) (sr * 0.25);
                driftTarget = rng.bi();
            }
            drift += (driftTarget - drift) * 0.00005f;

            wowPhase += wowRate / sr;
            if (wowPhase >= 1.0) wowPhase -= 1.0;
            flutterPhase += flutterRate / sr;
            if (flutterPhase >= 1.0) flutterPhase -= 1.0;

            const float mod = p.enabled
                ? wowDepth * (0.7f * (float) std::sin (2.0 * kPi * wowPhase) + 0.3f * drift)
                  + flutterDepth * (float) std::sin (2.0 * kPi * flutterPhase)
                : 0.0f;
            const float drive = driveSm.next();
            const float comp = 1.0f / std::pow (drive, 0.65f);

            for (int c = 0; c < 2; ++c)
            {
                auto& ch = chans[c];
                float x = io[c][i];

                if (p.enabled)
                {
                    x = ch.pre.process (x);
                    x = (std::tanh (drive * x + bias) - biasOut) * comp;
                    x = ch.de.process (x);
                    x = ch.dc.process (x);
                    x = ch.hp.process (ch.lp2.process (ch.lp1.process (x)));
                    x += ch.hissHp.process (rng.bi()) * hissLevel;
                }

                ch.line[(size_t) writePos] = x;
                const double readPos = (double) writePos - (double) baseDelay - (double) mod;
                io[c][i] = readCircular (ch.line.data(), lineSize, readPos);
            }

            if (++writePos >= lineSize) writePos = 0;
        }
    }

private:
    struct Channel
    {
        std::vector<float> line;
        Biquad pre, de, dc, lp1, lp2, hp, hissHp;
    };

    double sr = 44100.0;
    int baseDelay = 1, lineSize = 64, writePos = 0;
    Channel chans[2];
    double wowPhase = 0, flutterPhase = 0;
    float drift = 0, driftTarget = 0;
    int driftCounter = 0;
    float lastAge = -1.0f;
    Smoother driveSm;
    Rng rng { 0xBADC0DEu };
};

} // namespace granula
