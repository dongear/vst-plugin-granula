#pragma once

#include "HarmonicDsp.h"
#include <atomic>
#include <cstdint>

namespace interference
{

// Monophonic pitch correction ("auto-tune"):
//  - YIN pitch detection on a decimated mono signal
//  - target = nearest note of the active key, approached at the retune speed
//  - TD-PSOLA resynthesis: pitch-synchronous grains re-spaced at the new period,
//    which shifts pitch while keeping formants (optionally shifted on purpose)
class PitchCorrector
{
public:
    struct Params
    {
        uint16_t mask = 0x0AB5;
        float speedMs = 20.0f;   // 0 = instant ("robot")
        float humanize = 0.0f;   // 0..1, slower correction on sustained notes
        float amount = 1.0f;     // 0..1
        float formant = 0.0f;    // semitones, -7..+7
    };

    // UI read-outs
    std::atomic<float> detectedPitch { -1.0f }; // MIDI pitch (fractional) or -1 when unvoiced
    std::atomic<float> targetPitch { -1.0f };
    std::atomic<float> appliedShift { 0.0f };   // semitones

    int getLatency() const { return latency; }

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        dec = std::max (1, (int) std::lround (sr / 24000.0));
        dsr = sr / dec;
        latency = (int) std::lround (0.036 * sr);
        ringSize = 1;
        while (ringSize < latency * 6 + 4096) ringSize <<= 1;
        ringMask = ringSize - 1;
        for (auto& r : in) r.assign ((size_t) ringSize, 0.0f);
        for (auto& r : acc) r.assign ((size_t) ringSize, 0.0f);
        wsum.assign ((size_t) ringSize, 0.0f);
        dring.assign (kDetRing, 0.0f);
        aa1.setLowpass (sr, std::min (0.4 * dsr, sr * 0.45));
        aa2.setLowpass (sr, std::min (0.4 * dsr, sr * 0.45));
        tauMax = (int) (dsr / kMinHz) + 2;
        tauMin = (int) (dsr / kMaxHz);
        diff.assign ((size_t) tauMax + 2, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& r : in) std::fill (r.begin(), r.end(), 0.0f);
        for (auto& r : acc) std::fill (r.begin(), r.end(), 0.0f);
        std::fill (wsum.begin(), wsum.end(), 0.0f);
        std::fill (dring.begin(), dring.end(), 0.0f);
        aa1.reset(); aa2.reset();
        t = 0;
        dpos = 0; dcount = 0;
        voiced = false;
        period = 256.0;
        curShift = desiredShift = 0.0;
        markPos = 0;
        nextSynth = latency;   // first grain lands once enough input exists
        lastTarget = -1000.0; heldTime = 0.0;
        detectedPitch = -1.0f; targetPitch = -1.0f; appliedShift = 0.0f;
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        params = p;
        const float fr = std::pow (2.0f, std::clamp (p.formant, -7.0f, 7.0f) / 12.0f);
        float* io[2] = { L, R };

        for (int i = 0; i < n; ++i)
        {
            const int w = (int) (t & ringMask);
            in[0][(size_t) w] = L[i];
            in[1][(size_t) w] = R[i];

            // decimated mono for detection
            const float mono = aa2.process (aa1.process (0.5f * (L[i] + R[i])));
            if (t % dec == 0)
            {
                dring[(size_t) dpos] = mono;
                dpos = (dpos + 1) & (kDetRing - 1);
                if (++dcount >= kHop) { dcount = 0; detect(); }
            }

            // retune glide
            curShift = coef <= 0.0 ? desiredShift : desiredShift + (curShift - desiredShift) * coef;

            // schedule grains that will start being read soon
            while (nextSynth - grainHalf() <= t + 1) placeGrain (fr);

            // output (latency samples behind the input)
            for (int c = 0; c < 2; ++c)
            {
                const float ws = wsum[(size_t) w];
                io[c][i] = acc[c][(size_t) w] / std::max (ws, 0.6f);
                acc[c][(size_t) w] = 0.0f;
            }
            wsum[(size_t) w] = 0.0f;
            ++t;
        }
        appliedShift.store ((float) curShift, std::memory_order_relaxed);
    }

private:
    static constexpr double kMinHz = 70.0, kMaxHz = 1100.0;
    static constexpr int kDetRing = 2048, kHop = 128, kWin = 512;

    double grainHalf() const { return voiced ? period : 256.0; }

    void placeGrain (float fr)
    {
        const double P = grainHalf();
        const int64_t S = nextSynth;
        const int64_t a = S - latency; // analysis centre (input time)

        int64_t c = a;
        if (voiced)
        {
            const int64_t Pi = std::max<int64_t> (16, (int64_t) std::lround (period));
            if (a - markPos > 4 * Pi || markPos > a) markPos = a;      // (re)start the mark chain
            while (markPos + Pi <= a) markPos += Pi;
            c = (a - markPos) * 2 < Pi ? markPos : markPos + Pi;
        }

        const int half = (int) std::lround (P);
        const double maxRead = (double) (t - 2);
        for (int j = -half; j <= half; ++j)
        {
            const float wv = 0.5f + 0.5f * std::cos ((float) kPi * (float) j / (float) half);
            const double pos = std::min (maxRead, (double) c + (double) j * fr);
            const int64_t ip = (int64_t) std::floor (pos);
            const float fpart = (float) (pos - (double) ip);
            const size_t i0 = (size_t) (ip & ringMask), i1 = (size_t) ((ip + 1) & ringMask);
            const size_t o = (size_t) ((S + j) & ringMask);
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = in[ch][i0] + (in[ch][i1] - in[ch][i0]) * fpart;
                acc[ch][o] += x * wv;
            }
            wsum[o] += wv;
        }

        const double ratio = voiced ? std::pow (2.0, curShift / 12.0) : 1.0;
        nextSynth = S + std::max<int64_t> (8, (int64_t) std::lround (P / ratio));
    }

    // YIN on the last kWin + tauMax decimated samples
    void detect()
    {
        const int need = kWin + tauMax;
        auto x = [this, need] (int k) { return dring[(size_t) ((dpos - need + k) & (kDetRing - 1))]; };

        double energy = 0.0;
        for (int k = 0; k < kWin; ++k) energy += (double) x (k) * x (k);
        const double rms = std::sqrt (energy / kWin);

        bool isVoiced = false;
        double f0 = 0.0;
        if (rms > 0.003)
        {
            diff[0] = 1.0f;
            double running = 0.0;
            int best = -1;
            for (int tau = 1; tau <= tauMax; ++tau)
            {
                double d = 0.0;
                for (int k = 0; k < kWin; ++k)
                {
                    const double v = x (k) - x (k + tau);
                    d += v * v;
                }
                running += d;
                diff[(size_t) tau] = (float) (running > 0.0 ? d * tau / running : 1.0);
            }
            for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
                if (diff[(size_t) tau] < 0.15f)
                {
                    while (tau + 1 < tauMax && diff[(size_t) (tau + 1)] < diff[(size_t) tau]) ++tau;
                    best = tau;
                    break;
                }
            if (best < 0)
            {
                float m = 1.0e9f;
                for (int tau = std::max (2, tauMin); tau < tauMax; ++tau)
                    if (diff[(size_t) tau] < m) { m = diff[(size_t) tau]; best = tau; }
            }
            if (best > 1 && best < tauMax && diff[(size_t) best] < 0.3f)
            {
                const double s0 = diff[(size_t) (best - 1)], s1 = diff[(size_t) best], s2 = diff[(size_t) (best + 1)];
                const double den = s0 - 2.0 * s1 + s2;
                const double shift = std::abs (den) > 1.0e-9 ? 0.5 * (s0 - s2) / den : 0.0;
                f0 = dsr / ((double) best + std::clamp (shift, -1.0, 1.0));
                isVoiced = f0 >= kMinHz && f0 <= kMaxHz;
            }
        }

        const double hopSec = (double) kHop / dsr;
        if (isVoiced)
        {
            const double newPeriod = sr / f0;
            period = (voiced && std::abs (newPeriod / period - 1.0) < 0.08) ? 0.5 * (period + newPeriod) : newPeriod;
            const double pitch = freqToPitch (f0);
            double target;
            nearestInKey (pitch, params.mask, target);
            if ((params.mask & 0xFFF) == 0) target = pitch;
            desiredShift = (target - pitch) * std::clamp ((double) params.amount, 0.0, 1.0);

            heldTime = std::abs (target - lastTarget) < 0.5 ? heldTime + hopSec : 0.0;
            lastTarget = target;
            const double sustain = std::clamp ((heldTime - 0.15) / 0.3, 0.0, 1.0);
            const double speed = params.speedMs * (1.0 + 5.0 * params.humanize * sustain) + 25.0 * params.humanize * sustain;
            coef = speed < 0.5 ? 0.0 : std::exp (-1.0 / (speed * 0.001 * sr));

            detectedPitch.store ((float) pitch, std::memory_order_relaxed);
            targetPitch.store ((float) target, std::memory_order_relaxed);
        }
        else
        {
            desiredShift = 0.0;
            heldTime = 0.0;
            lastTarget = -1000.0;
            coef = std::exp (-1.0 / (0.02 * sr));
            detectedPitch.store (-1.0f, std::memory_order_relaxed);
            targetPitch.store (-1.0f, std::memory_order_relaxed);
        }
        voiced = isVoiced;
    }

    double sr = 48000.0, dsr = 24000.0;
    int dec = 2, latency = 1728, ringSize = 16384, ringMask = 16383;
    std::vector<float> in[2], acc[2], wsum, dring, diff;
    granula::Biquad aa1, aa2;
    int tauMin = 20, tauMax = 350;
    int64_t t = 0, markPos = 0, nextSynth = 0;
    int dpos = 0, dcount = 0;
    bool voiced = false;
    double period = 256.0, curShift = 0.0, desiredShift = 0.0, coef = 0.0;
    double lastTarget = -1000.0, heldTime = 0.0;
    Params params;
};

} // namespace interference
