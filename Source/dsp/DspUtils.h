#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace granula
{
constexpr double kPi = 3.14159265358979323846;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// Fast, deterministic RNG (xorshift32) — safe to use on the audio thread.
struct Rng
{
    uint32_t state = 0x9E3779B9u;

    explicit Rng (uint32_t seed = 0x9E3779B9u) : state (seed ? seed : 1u) {}

    inline uint32_t next()
    {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state = x;
    }

    // [0, 1)
    inline float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    // [-1, 1)
    inline float bi() { return uni() * 2.0f - 1.0f; }
};

// 4-point, 3rd-order Hermite interpolation.
inline float hermite (float xm1, float x0, float x1, float x2, float t)
{
    const float c = (x1 - xm1) * 0.5f;
    const float v = x0 - x1;
    const float w = c + v;
    const float a = w + v + (x2 - x0) * 0.5f;
    const float bNeg = w + a;
    return ((((a * t) - bNeg) * t + c) * t + x0);
}

// Reads a circular buffer at a fractional position with Hermite interpolation.
inline float readCircular (const float* buf, int size, double pos)
{
    double fl = std::floor (pos);
    const float t = (float) (pos - fl);
    int i = (int) fl % size;
    if (i < 0) i += size;
    const int im1 = (i - 1 + size) % size;
    const int i1 = (i + 1) % size;
    const int i2 = (i + 2) % size;
    return hermite (buf[im1], buf[i], buf[i1], buf[i2], t);
}

// RBJ biquad, transposed direct form II.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }

    inline float process (float xIn)
    {
        const double x = xIn;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    void setLowpass (double sr, double f, double q = 0.7071)
    {
        f = std::clamp (f, 10.0, sr * 0.49);
        const double w0 = 2.0 * kPi * f / sr, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 - c) * 0.5 / a0; b1 = (1.0 - c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }

    void setHighpass (double sr, double f, double q = 0.7071)
    {
        f = std::clamp (f, 5.0, sr * 0.49);
        const double w0 = 2.0 * kPi * f / sr, c = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = b0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }

    void setHighShelf (double sr, double f, double gainDb)
    {
        f = std::clamp (f, 10.0, sr * 0.49);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * f / sr, c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / 2.0 * std::sqrt (2.0); // shelf slope S = 1
        const double sqA = 2.0 * std::sqrt (A) * alpha;
        const double a0 = (A + 1) - (A - 1) * c + sqA;
        b0 = A * ((A + 1) + (A - 1) * c + sqA) / a0;
        b1 = -2.0 * A * ((A - 1) + (A + 1) * c) / a0;
        b2 = A * ((A + 1) + (A - 1) * c - sqA) / a0;
        a1 = 2.0 * ((A - 1) - (A + 1) * c) / a0;
        a2 = ((A + 1) - (A - 1) * c - sqA) / a0;
    }
};

// Simple one-pole smoother for control values.
struct Smoother
{
    float value = 0, target = 0, coeff = 0.999f;

    void prepare (double sr, float timeMs)
    {
        coeff = (float) std::exp (-1.0 / (sr * timeMs * 0.001));
    }
    void snap (float v) { value = target = v; }
    inline float next() { value = target + (value - target) * coeff; return value; }
};

// Mix law used by delay / reverb: both signals at full level around 50%.
inline void wetDryGains (float mix, float& dry, float& wet)
{
    dry = std::min (1.0f, 2.0f * (1.0f - mix));
    wet = std::min (1.0f, 2.0f * mix);
}

} // namespace granula
