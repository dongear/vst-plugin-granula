#pragma once

#include <juce_dsp/juce_dsp.h>
#include "../../Source/dsp/DspUtils.h"
#include <array>
#include <complex>
#include <memory>
#include <vector>

// Harmonic "sweetening": pulls audio toward the notes of a chosen key.
//  - FilterBank: zero-latency bank of resonant band-passes on the in-key notes
//  - SpectralEngine: phase-vocoder that attenuates or *moves* out-of-key energy
namespace interference
{
using granula::kPi;
using granula::Biquad;

inline double noteFreq (double midi) { return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0); }
inline double freqToPitch (double f) { return 69.0 + 12.0 * std::log2 (std::max (1.0e-3, f) / 440.0); }

// Distance in semitones from pitch p to the nearest pitch class in mask (bit 0 = C).
inline double nearestInKey (double p, uint16_t mask, double& target)
{
    target = p;
    if ((mask & 0xFFF) == 0) return 0.0;
    const int base = (int) std::floor (p);
    double best = 1.0e9;
    for (int o = -7; o <= 8; ++o)
    {
        const int n = base + o;
        const int pc = ((n % 12) + 12) % 12;
        if (! (mask & (1u << pc))) continue;
        const double d = std::abs (p - (double) n);
        if (d < best) { best = d; target = (double) n; }
    }
    return best;
}

struct Params
{
    uint16_t mask = 0x0AB5;  // C major
    float color = 1.0f;      // 0..2  (0..1 = amount, 1..2 = resonant tail)
    float snap = 0.0f;       // 0..1  (HQ only: move out-of-key energy into key)
    float focus = 0.5f;      // 0..1  precision
    float gate = 0.0f;       // 0..1  spectral gate
    float lowHz = 60.0f, highHz = 16000.0f;
};

// ============================================================================
class FilterBank
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& s : slots) s = Slot {};
        lastQ = -1.0; lastMask = 0xFFFF; lastLo = lastHi = -1.0f;
        wSm.prepare (sr, 30.0f);
        wSm.snap (1.0f);
        reset();
    }

    void reset()
    {
        for (auto& s : slots)
        {
            for (int c = 0; c < 2; ++c) s.z1[c] = s.z2[c] = s.z3[c] = s.z4[c] = 0.0;
            s.env = 0.0f; s.gain = s.target; s.gate = 1.0f;
        }
        for (auto& c : xo) for (auto& b : c) b.reset();
        gateCounter = 0;
    }

    void process (float* L, float* R, int n, const Params& p)
    {
        const double c = std::clamp ((double) p.color, 0.0, 2.0);
        // Per-stage Q (each band is two cascaded band-passes = 4th order skirts).
        const double qBase = 12.0 + 40.0 * std::clamp ((double) p.focus, 0.0, 1.0);
        const double q = c > 1.0 ? qBase * std::pow (400.0 / qBase, c - 1.0) : qBase;
        const float lo = std::min (p.lowHz, p.highHz * 0.9f), hi = p.highHz;

        // Slot targets (in key & in range)
        bool setChanged = p.mask != lastMask || ! juce::exactlyEqual (lo, lastLo) || ! juce::exactlyEqual (hi, lastHi);
        if (setChanged)
            for (int m = 0; m < 128; ++m)
            {
                auto& s = slots[(size_t) m];
                const double f = noteFreq (m);
                s.valid = f > 15.0 && f < sr * 0.45;
                s.target = (s.valid && (p.mask & (1u << (m % 12))) && f >= lo && f <= hi) ? 1.0f : 0.0f;
            }

        if (std::abs (q - lastQ) > 1.0e-6 || setChanged)
        {
            for (int m = 0; m < 128; ++m)
            {
                auto& s = slots[(size_t) m];
                if (! s.valid) continue;
                const double w0 = 2.0 * kPi * noteFreq (m) / sr;
                const double alpha = std::sin (w0) / (2.0 * q);
                const double a0 = 1.0 + alpha;
                s.b0 = alpha / a0;
                s.a1 = -2.0 * std::cos (w0) / a0;
                s.a2 = (1.0 - alpha) / a0;
            }
            computeNorm();
            lastQ = q;
        }

        if (! juce::exactlyEqual (lo, lastLo) || ! juce::exactlyEqual (hi, lastHi))
        {
            for (auto& x : xo)
            {
                x[0].setLowpass (sr, lo); x[1].setLowpass (sr, lo);
                x[2].setHighpass (sr, hi); x[3].setHighpass (sr, hi);
            }
        }
        lastMask = p.mask; lastLo = lo; lastHi = hi;

        // Active list
        int nAct = 0;
        for (int m = 0; m < 128; ++m)
            if (slots[(size_t) m].valid && (slots[(size_t) m].target > 0.0f || slots[(size_t) m].gain > 0.0f))
                active[(size_t) nAct++] = m;

        const float ramp = 1.0f / (float) (0.01 * sr);
        const float envAtt = (float) std::exp (-1.0 / (0.004 * sr)), envRel = (float) std::exp (-1.0 / (0.08 * sr));
        const float thrRel = p.gate > 0.001f ? granula::dbToGain (-42.0f + 38.0f * p.gate) : 0.0f;
        wSm.target = (float) std::min (c, 1.0);

        for (int i = 0; i < n; ++i)
        {
            const double xL = L[i], xR = R[i];
            const double lowL = xo[0][1].process (xo[0][0].process ((float) xL));
            const double lowR = xo[1][1].process (xo[1][0].process ((float) xR));
            const double highL = xo[0][3].process (xo[0][2].process ((float) xL));
            const double highR = xo[1][3].process (xo[1][2].process ((float) xR));

            double wetL = 0.0, wetR = 0.0;
            for (int a = 0; a < nAct; ++a)
            {
                auto& s = slots[(size_t) active[(size_t) a]];
                const double uL = s.b0 * xL + s.z1[0];
                s.z1[0] = -s.a1 * uL + s.z2[0];
                s.z2[0] = -s.b0 * xL - s.a2 * uL;
                const double uR = s.b0 * xR + s.z1[1];
                s.z1[1] = -s.a1 * uR + s.z2[1];
                s.z2[1] = -s.b0 * xR - s.a2 * uR;
                const double yL = s.b0 * uL + s.z3[0];
                s.z3[0] = -s.a1 * yL + s.z4[0];
                s.z4[0] = -s.b0 * uL - s.a2 * yL;
                const double yR = s.b0 * uR + s.z3[1];
                s.z3[1] = -s.a1 * yR + s.z4[1];
                s.z4[1] = -s.b0 * uR - s.a2 * yR;

                const float g = s.gain * s.gate;
                wetL += yL * g;
                wetR += yR * g;

                const float e = (float) (std::abs (yL) + std::abs (yR));
                s.env = e > s.env ? e + (s.env - e) * envAtt : e + (s.env - e) * envRel;

                if (s.gain < s.target) s.gain = std::min (s.target, s.gain + ramp);
                else if (s.gain > s.target) s.gain = std::max (s.target, s.gain - ramp);
            }

            if (++gateCounter >= 32)
            {
                gateCounter = 0;
                float maxEnv = 1.0e-9f;
                for (int a = 0; a < nAct; ++a) maxEnv = std::max (maxEnv, slots[(size_t) active[(size_t) a]].env);
                for (int a = 0; a < nAct; ++a)
                {
                    auto& s = slots[(size_t) active[(size_t) a]];
                    float t = 1.0f;
                    if (thrRel > 0.0f)
                    {
                        t = std::clamp (s.env / (maxEnv * thrRel), 0.0f, 1.0f);
                        t *= t;
                    }
                    s.gate += (t - s.gate) * 0.35f;
                }
            }

            const double w = wSm.next();
            const double midL = xL - lowL - highL, midR = xR - lowR - highR;
            L[i] = (float) (lowL + highL + midL * (1.0 - w) + wetL * norm * w);
            R[i] = (float) (lowR + highR + midR * (1.0 - w) + wetR * norm * w);
        }
    }

private:
    struct Slot
    {
        double b0 = 0, a1 = 0, a2 = 0;
        double z1[2] {}, z2[2] {}, z3[2] {}, z4[2] {};
        float gain = 0, target = 0, env = 0, gate = 1;
        bool valid = false;
    };

    // Unity gain at the in-key note centres (summed neighbouring skirts included).
    void computeNorm()
    {
        std::vector<int> act;
        for (int m = 0; m < 128; ++m)
            if (slots[(size_t) m].valid && slots[(size_t) m].target > 0.0f) act.push_back (m);
        if (act.empty()) { norm = 0.0; return; }

        double acc = 0.0;
        for (int j : act)
        {
            const double w = 2.0 * kPi * noteFreq (j) / sr;
            const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
            std::complex<double> h = 0.0;
            for (int k : act)
            {
                const auto& s = slots[(size_t) k];
                const auto hk = (s.b0 - s.b0 * z2) / (1.0 + s.a1 * z1 + s.a2 * z2);
                h += hk * hk;
            }
            acc += std::norm (h);
        }
        norm = 1.0 / std::sqrt (std::max (1.0e-12, acc / (double) act.size()));
    }

    double sr = 48000.0;
    std::array<Slot, 128> slots {};
    std::array<int, 128> active {};
    std::array<std::array<Biquad, 4>, 2> xo {};
    double norm = 1.0, lastQ = -1.0;
    uint16_t lastMask = 0xFFFF;
    float lastLo = -1, lastHi = -1;
    int gateCounter = 0;
    granula::Smoother wSm;
};

// ============================================================================
class SpectralEngine
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sr > 60000.0 ? 13 : 12;
        N = 1 << order;
        hop = N / 4;
        latency = N - hop;      // FIFO rover offset
        fft = std::make_unique<juce::dsp::FFT> (order);
        window.resize ((size_t) N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / N));
        buf.assign ((size_t) N * 2, 0.0f);
        const int nb = N / 2 + 1;
        outRe.assign ((size_t) nb, 0.0f); outIm.assign ((size_t) nb, 0.0f);
        binNote.assign ((size_t) nb, -1); binNew.assign ((size_t) nb, 0.0f); binMag.assign ((size_t) nb, 0.0f); binPh.assign ((size_t) nb, 0.0f);
        binDest.assign ((size_t) nb, 0);
        for (auto& c : ch)
        {
            c.inFifo.assign ((size_t) N, 0.0f); c.outFifo.assign ((size_t) N, 0.0f); c.accum.assign ((size_t) N * 2, 0.0f);
            c.lastPhase.assign ((size_t) nb, 0.0f); c.sumPhase.assign ((size_t) nb, 0.0f);
            c.tailMag.assign ((size_t) nb, 0.0f); c.tailFreq.assign ((size_t) nb, 0.0f); c.tailPhase.assign ((size_t) nb, 0.0f);
        }
        reset();
    }

    int getLatency() const { return N; } // measured: input -> output delay of the OLA pipeline

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.inFifo.begin(), c.inFifo.end(), 0.0f);
            std::fill (c.outFifo.begin(), c.outFifo.end(), 0.0f);
            std::fill (c.accum.begin(), c.accum.end(), 0.0f);
            std::fill (c.lastPhase.begin(), c.lastPhase.end(), 0.0f);
            std::fill (c.sumPhase.begin(), c.sumPhase.end(), 0.0f);
            std::fill (c.tailMag.begin(), c.tailMag.end(), 0.0f);
            c.notePhase.fill (0.0f);
            c.noteActive.fill (false);
        }
        rover = latency;
    }

    // Produces the fully processed ("wet") signal, delayed by getLatency().
    void process (float* L, float* R, int n, const Params& p)
    {
        float* io[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                ch[c].inFifo[(size_t) rover] = io[c][i];
                io[c][i] = ch[c].outFifo[(size_t) (rover - latency)];
            }
            if (++rover >= N)
            {
                rover = latency;
                for (int c = 0; c < 2; ++c) frame (ch[c], p);
            }
        }
    }

private:
    struct Chan
    {
        std::vector<float> inFifo, outFifo, accum;
        std::vector<float> lastPhase, sumPhase, tailMag, tailFreq, tailPhase;
        std::array<float, 160> notePhase {};
        std::array<bool, 160> noteActive {};
    };

    static inline float wrap (float x)
    {
        x = std::fmod (x + (float) kPi, 2.0f * (float) kPi);
        if (x < 0) x += 2.0f * (float) kPi;
        return x - (float) kPi;
    }

    void frame (Chan& c, const Params& p)
    {
        const int nb = N / 2;
        const double binHz = sr / N;
        const float osamp = (float) N / (float) hop;
        const float expct = 2.0f * (float) kPi * (float) hop / (float) N;

        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = c.inFifo[(size_t) i] * window[(size_t) i];
        fft->performRealOnlyForwardTransform (buf.data(), true);

        const int kLo = std::max (1, (int) std::ceil (p.lowHz / binHz));
        const int kHi = std::min (nb - 1, (int) std::floor (p.highHz / binHz));
        const double sigma = 0.6 - 0.45 * std::clamp ((double) p.focus, 0.0, 1.0);
        const float snap = std::clamp (p.snap, 0.0f, 1.0f);
        const bool pv = snap > 0.001f;
        const float c2 = std::clamp (p.color, 0.0f, 2.0f);
        const bool tails = c2 > 1.0f;
        const double tailT = 0.15 + (c2 - 1.0) * 5.0;
        const float tailDecay = tails ? (float) std::pow (10.0, -3.0 * hop / (sr * tailT)) : 0.0f;

        // Spectral gate threshold relative to the loudest in-range bin
        float peak = 0.0f;
        for (int k = kLo; k <= kHi; ++k)
            peak = std::max (peak, std::hypot (buf[(size_t) (2 * k)], buf[(size_t) (2 * k + 1)]));
        const float gateThr = p.gate > 0.001f ? peak * granula::dbToGain (-60.0f + 54.0f * p.gate) : 0.0f;

        if (pv)
        {
            std::fill (outRe.begin(), outRe.end(), 0.0f);
            std::fill (outIm.begin(), outIm.end(), 0.0f);
            std::fill (binNote.begin(), binNote.end(), -1);
        }

        for (int k = 1; k < nb; ++k)
        {
            const float re = buf[(size_t) (2 * k)], im = buf[(size_t) (2 * k + 1)];
            const float mag = std::hypot (re, im);
            const float ph = std::atan2 (im, re);

            float d = ph - c.lastPhase[(size_t) k];
            c.lastPhase[(size_t) k] = ph;
            d = wrap (d - (float) k * expct);
            const float trueBins = (float) k + d * osamp / (2.0f * (float) kPi);

            if (k < kLo || k > kHi) continue; // passes untouched

            const float gateF = gateThr > 0.0f && mag < gateThr ? (mag / gateThr) * (mag / gateThr) : 1.0f;
            const double fHz = std::max (0.5f, trueBins) * binHz;
            const double pitch = freqToPitch (fHz);
            double target;
            const double dist = nearestInKey (pitch, p.mask, target);

            // Bins wider than a semitone can't tell notes apart: leave them alone.
            const double semitoneHz = fHz * 0.0595;
            const float res = (float) std::clamp (semitoneHz / binHz - 0.5, 0.0, 1.0);
            const float gauss = (float) std::exp (-0.5 * (dist / sigma) * (dist / sigma));

            if (! pv)
            {
                const float g = ((1.0f - res) + res * gauss) * gateF;
                float oRe = re * g, oIm = im * g;
                const float m = mag * g;
                if (tails) applyTail (c, k, m, trueBins, ph, gauss, tailDecay, oRe, oIm);
                else c.tailMag[(size_t) k] = 0.0f;
                buf[(size_t) (2 * k)] = oRe;
                buf[(size_t) (2 * k + 1)] = oIm;
            }
            else
            {
                const double shift = (target - pitch) * snap * res;
                const float g = ((1.0f - res) + res * (gauss + (1.0f - gauss) * snap)) * gateF;
                const double fNew = fHz * std::pow (2.0, shift / 12.0);
                // Shift each bin by its partial's offset so window lobes keep their shape.
                binDest[(size_t) k] = std::clamp ((int) std::lround ((double) k + (fNew - fHz) / binHz), kLo, kHi);
                binNote[(size_t) k] = std::clamp ((int) target, 0, 159);
                binNew[(size_t) k] = (float) fNew;
                binMag[(size_t) k] = mag * g;
                binPh[(size_t) k] = ph;
            }
        }

        if (pv)
        {
            // Phase locking per destination note: every bin of a note keeps its analysed
            // phase offset to the note's loudest bin, which advances at the new frequency.
            std::array<int, 160> peakBin;
            peakBin.fill (-1);
            for (int k = kLo; k <= kHi; ++k)
            {
                const int q = binNote[(size_t) k];
                if (q >= 0 && (peakBin[(size_t) q] < 0 || binMag[(size_t) k] > binMag[(size_t) peakBin[(size_t) q]])) peakBin[(size_t) q] = k;
            }
            for (int q = 0; q < 160; ++q)
            {
                const int pk = peakBin[(size_t) q];
                if (pk < 0) { c.noteActive[(size_t) q] = false; continue; }
                if (! c.noteActive[(size_t) q]) c.notePhase[(size_t) q] = binPh[(size_t) pk];
                else c.notePhase[(size_t) q] = wrap (c.notePhase[(size_t) q] + 2.0f * (float) kPi * binNew[(size_t) pk] * (float) hop / (float) sr);
                c.noteActive[(size_t) q] = true;
            }
            for (int k = kLo; k <= kHi; ++k)
            {
                const int q = binNote[(size_t) k];
                if (q < 0) continue;
                const float phOut = binPh[(size_t) k] - binPh[(size_t) peakBin[(size_t) q]] + c.notePhase[(size_t) q];
                const int d = binDest[(size_t) k];
                outRe[(size_t) d] += binMag[(size_t) k] * std::cos (phOut);
                outIm[(size_t) d] += binMag[(size_t) k] * std::sin (phOut);
            }
            for (int k = kLo; k <= kHi; ++k)
            {
                float oRe = outRe[(size_t) k], oIm = outIm[(size_t) k];
                if (tails)
                {
                    const float m = std::hypot (oRe, oIm);
                    const int q = binNote[(size_t) k];
                    const float fb = q >= 0 ? (float) (noteFreq (q) / binHz) : (float) k;
                    applyTail (c, k, m, fb, std::atan2 (oIm, oRe), 1.0f, tailDecay, oRe, oIm);
                }
                else c.tailMag[(size_t) k] = 0.0f;
                buf[(size_t) (2 * k)] = oRe;
                buf[(size_t) (2 * k + 1)] = oIm;
            }
        }

        buf[0] = buf[1] = 0.0f; // DC
        fft->performRealOnlyInverseTransform (buf.data());

        const float ola = 1.0f / 1.5f; // Hann^2 at 75% overlap sums to 1.5
        for (int i = 0; i < N; ++i) c.accum[(size_t) i] += buf[(size_t) i] * window[(size_t) i] * ola;
        for (int i = 0; i < hop; ++i) c.outFifo[(size_t) i] = c.accum[(size_t) i];
        std::memmove (c.accum.data(), c.accum.data() + hop, sizeof (float) * (size_t) (2 * N - hop));
        std::fill (c.accum.begin() + (2 * N - hop), c.accum.end(), 0.0f);
        std::memmove (c.inFifo.data(), c.inFifo.data() + hop, sizeof (float) * (size_t) latency);
    }

    // "Color" above 100%: in-key partials keep ringing and decay slowly.
    void applyTail (Chan& c, int k, float m, float freqBins, float phase, float inKey, float decay,
                    float& resRe, float& resIm)
    {
        float& tm = c.tailMag[(size_t) k];
        const float held = tm * decay;
        if (held > 2.0f * m && held > 1.0e-7f) // the sound dropped: let the in-key partial ring on
        {
            tm = held;
            float& tp = c.tailPhase[(size_t) k];
            tp = wrap (tp + 2.0f * (float) kPi * c.tailFreq[(size_t) k] * (float) hop / (float) N);
            resRe = held * std::cos (tp);
            resIm = held * std::sin (tp);
        }
        else if (inKey > 0.5f)
        {
            tm = m * 0.8f;
            c.tailFreq[(size_t) k] = freqBins;
            c.tailPhase[(size_t) k] = phase;
        }
        else tm = 0.0f;
    }

    double sr = 48000.0;
    int order = 12, N = 4096, hop = 1024, latency = 3072, rover = 3072;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, buf, outRe, outIm, binNew, binMag, binPh;
    std::vector<int> binNote, binDest;
    Chan ch[2];
};

} // namespace interference
