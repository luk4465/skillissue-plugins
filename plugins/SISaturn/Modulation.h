#pragma once
#include <cmath>
#include <array>
#include <algorithm>

namespace si::sat
{
constexpr int kModSources = 4;
constexpr int kModSlots = 12;
enum class ModType { None = 0, XLFO, Envelope, Follower, Transients, Slider };

struct ModSourceParams
{
    ModType type = ModType::None;
    float rateHz = 1.0f;
    int sync = 0;              // 0 = free, else index into syncBeats()
    int shape = 0;             // 0 sine, 1 tri, 2 saw up, 3 saw down, 4 square, 5 random, 6 steps
    float attackMs = 5.0f, decayMs = 200.0f, sustain = 0.5f, releaseMs = 300.0f, thresholdDb = -30.0f;
    int input = 0;             // 0 = main input, 1..6 = band input
    float value = 0.5f;        // slider
};

inline double syncBeats (int i)
{
    static const double b[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 };
    return b[std::min (7, std::max (0, i))];
}

/** One modulation source, advanced once per processing chunk. Output: LFO -1..1, others 0..1. */
class ModSource
{
public:
    float advance (const ModSourceParams& p, double dt, double bpm, float inputLevel)
    {
        switch (p.type)
        {
            case ModType::None: out = 0.0f; break;
            case ModType::Slider: out = p.value; break;
            case ModType::XLFO:
            {
                const double hz = p.sync > 0 ? (bpm / 60.0) / syncBeats (p.sync) : (double) p.rateHz;
                const double prev = phase;
                phase += hz * dt;
                const bool wrapped = phase >= 1.0;
                phase -= std::floor (phase);
                if (wrapped || prev == 0.0) { rndPrev = rndNext; rndNext = nextRandom(); ++step; }
                const float ph = (float) phase;
                switch (p.shape)
                {
                    case 0: out = std::sin (ph * 6.2831853f); break;
                    case 1: out = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                    case 2: out = 2.0f * ph - 1.0f; break;
                    case 3: out = 1.0f - 2.0f * ph; break;
                    case 4: out = ph < 0.5f ? 1.0f : -1.0f; break;
                    case 5: out = rndPrev + (rndNext - rndPrev) * (0.5f - 0.5f * std::cos (ph * 3.14159265f)); break;
                    default:
                    {
                        static const float pattern[16] = { 1.0f, -0.5f, 0.25f, -1.0f, 0.75f, 0.0f, -0.75f, 0.5f,
                                                           1.0f, -0.25f, 0.5f, -1.0f, 0.25f, 0.75f, -0.5f, 0.0f };
                        out = pattern[step & 15];
                        break;
                    }
                }
                break;
            }
            case ModType::Envelope:
            {
                const float db = 20.0f * std::log10 (std::max (1e-6f, inputLevel));
                const bool above = db > p.thresholdDb;
                if (above && ! gate) stage = 1;          // attack
                if (! above && gate) stage = 4;          // release
                gate = above;
                const float ms = (float) (dt * 1000.0);
                if (stage == 1) { out += ms / std::max (0.1f, p.attackMs); if (out >= 1.0f) { out = 1.0f; stage = 2; } }
                else if (stage == 2) { out -= (1.0f - p.sustain) * ms / std::max (1.0f, p.decayMs); if (out <= p.sustain) { out = p.sustain; stage = 3; } }
                else if (stage == 4) { out -= std::max (0.0f, out) * ms / std::max (1.0f, p.releaseMs) * 4.0f; if (out < 0.001f) { out = 0.0f; stage = 0; } }
                break;
            }
            case ModType::Follower:
            case ModType::Transients:
            {
                const float ms = (float) (dt * 1000.0);
                const float a = 1.0f - std::exp (-ms / std::max (0.1f, p.attackMs));
                const float r = 1.0f - std::exp (-ms / std::max (1.0f, p.releaseMs));
                fast += (inputLevel - fast) * (inputLevel > fast ? a : r);
                slow += (inputLevel - slow) * (1.0f - std::exp (-ms / 120.0f));
                if (p.type == ModType::Follower)
                    out = std::clamp ((20.0f * std::log10 (std::max (1e-6f, fast)) + 60.0f) / 60.0f, 0.0f, 1.0f);
                else
                    out = std::clamp ((fast - slow) * 8.0f / std::max (0.02f, slow + 0.02f), 0.0f, 1.0f);
                break;
            }
        }
        return out;
    }
    float value() const { return out; }
    bool bipolar (const ModSourceParams& p) const { return p.type == ModType::XLFO; }

private:
    float nextRandom() { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xFFFF) / 32767.5f - 1.0f; }
    double phase = 0.0;
    float out = 0.0f, rndPrev = 0.0f, rndNext = 0.0f, fast = 0.0f, slow = 0.0f;
    int stage = 0, step = 0;
    bool gate = false;
    unsigned seed = 77u;
};
} // namespace si::sat
