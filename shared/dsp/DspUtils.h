#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace si::dsp
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }

/** Cheap tanh-like soft clip. */
inline float softClip (float x)
{
    x = clampf (x, -3.0f, 3.0f);
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

/** One-pole low-pass (also gives the high band as x - lp). */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, float sr) { a = std::exp (-kTwoPi * clampf (hz, 5.0f, sr * 0.49f) / sr); }
    float process (float x) { z = x + a * (z - x); return z; }
    void reset() { z = 0.0f; }
};

/** TPT state-variable filter (Zavalishin). */
struct Svf
{
    float g = 0, k = 1.414f, a1 = 0, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
    void set (float hz, float q, float sr)
    {
        g = std::tan (kPi * clampf (hz, 10.0f, sr * 0.49f) / sr);
        k = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns low, band, high via refs
    inline void tick (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float lowpass (float x)  { float l, b, h; tick (x, l, b, h); return l; }
    float highpass (float x) { float l, b, h; tick (x, l, b, h); return h; }
    float bandpass (float x) { float l, b, h; tick (x, l, b, h); return b; }
};

/** Power-of-two circular delay line with fractional (cubic) read. */
struct DelayLine
{
    std::vector<float> buf;
    uint32_t mask = 0, w = 0;

    void allocate (int maxSamples)
    {
        uint32_t n = 1;
        while (n < (uint32_t) maxSamples + 4) n <<= 1;
        buf.assign (n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int size() const { return (int) buf.size(); }

    inline void push (float x) { buf[w] = x; w = (w + 1) & mask; }

    /** Integer read, delay >= 1 (1 = last pushed sample). */
    inline float tap (int d) const { return buf[(w - (uint32_t) d) & mask]; }

    /** Cubic Hermite read; d >= 2. */
    inline float read (float d) const
    {
        const int di = (int) d;
        const float f = d - (float) di;
        const float xm1 = tap (di - 1), x0 = tap (di), x1 = tap (di + 1), x2 = tap (di + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
};

/** Schroeder all-pass with optionally modulated delay. */
struct Allpass
{
    DelayLine dl;
    float delay = 100.0f, g = 0.5f;
    void allocate (int maxSamples) { dl.allocate (maxSamples); }
    void clear() { dl.clear(); }
    inline float process (float x, float modOffset = 0.0f)
    {
        const float d = std::max (2.0f, delay + modOffset);
        const float v = dl.read (d);
        const float in = x + g * v;
        dl.push (in);
        return v - g * in;
    }
};

/** Small fast PRNG. */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 1) : s (seed * 2654435761u + 1u) {}
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 8388607.5f - 1.0f; }
};

/** Smoothed random LFO (cosine-interpolated random targets), output -1..1. */
struct RandomLfo
{
    Rng rng;
    float phase = 1.0f, a = 0, b = 0;
    explicit RandomLfo (uint32_t seed = 7) : rng (seed) {}
    inline float tick (float inc)
    {
        phase += inc;
        if (phase >= 1.0f) { phase -= std::floor (phase); a = b; b = rng.next(); }
        const float t = 0.5f - 0.5f * std::cos (phase * kPi);
        return a + (b - a) * t;
    }
};

struct Smoother
{
    float cur = 0, target = 0, coef = 0.999f;
    void setTime (float seconds, float sr) { coef = std::exp (-1.0f / std::max (1.0f, seconds * sr)); }
    void reset (float v) { cur = target = v; }
    inline float tick() { cur = target + coef * (cur - target); return cur; }
};
} // namespace si::dsp
