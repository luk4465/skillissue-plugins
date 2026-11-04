#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace si::sat
{
constexpr int kMaxBands = 6;
constexpr int kNumStyles = 28;
const char* styleName (int s);
const char* styleCategory (int s);         // Tube, Tape, Saturation, Amp, Transformer, FX
/** Static curve of a style for the UI (memory-less approximation of the band processor). */
float shape (int style, float x);

struct BandSettings
{
    bool on = true, mute = false, solo = false;
    int style = 6;                // Warm Tape
    float driveDb = 6.0f, drivePan = 0.0f;
    float mix = 1.0f;
    float feedback = 0.0f, fbFreq = 1000.0f;
    float dyn = 0.0f;             // -1 gate/expand .. +1 pumping compression
    float bass = 0.0f, mid = 0.0f, treble = 0.0f, presence = 0.0f;   // dB
    float levelDb = 0.0f, levelPan = 0.0f;
};

struct Settings
{
    int bands = 1;
    std::array<float, kMaxBands - 1> xo { 120.0f, 600.0f, 2500.0f, 6000.0f, 12000.0f };
    std::array<int, kMaxBands - 1> slope { 2, 2, 2, 2, 2 };   // 0=6, 1=12, 2=24, 3=48 dB/oct
    std::array<BandSettings, kMaxBands> band;
    bool autoGain = true, midSide = false;
    float mix = 1.0f;
};

/** 2nd-order TPT SVF stage (stereo). */
struct Svf2
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.414f, m0 = 0, m1 = 0, m2 = 1;
    float s1[2] {}, s2[2] {};
    void lp (float g, float q) { set (g, 1.0f / q, 0, 0, 1); }
    void hp (float g, float q) { set (g, 1.0f / q, 1, -1.0f / q, -1); }
    void set (float g, float kk, float mm0, float mm1, float mm2)
    {
        k = kk; m0 = mm0; m1 = mm1; m2 = mm2;
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline float tick (float x, int c)
    {
        const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3;
        s1[c] = 2.0f * v1 - s1[c];
        s2[c] = 2.0f * v2 - s2[c];
        return m0 * x + m1 * v1 + m2 * v2;
    }
    void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0.0f; }
};

/** Linkwitz-Riley style crossover of selectable order; lo+hi is flat (allpass). */
struct Crossover
{
    int order = 2;                         // 0=6, 1=12, 2=24, 3=48
    std::array<Svf2, 4> lo, hi;            // up to 4 2nd-order stages per path
    float G1 = 0, z[4][2] {};              // first-order TPT stages (6 / 12 dB)
    void set (float fc, float sr, int order);
    void process (float x, int c, float& l, float& h);
    float allpass (float x, int c) { float l, h; process (x, c, l, h); return l + h; }
    void reset();
};

/** Per band, per channel state for the style processors. */
struct StyleState
{
    float z[8] {};
    std::vector<float> dly;   // smudge delay
    int dw = 0, hold = 0;
    float held = 0.0f, flip = 1.0f, lastSign = 1.0f, rnd = 0.0f;
    unsigned seed = 1234567u;
};

class SaturnEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s) { set = s; }
    void process (float* l, float* r, int n);

    std::array<std::atomic<float>, kMaxBands> activity {};   // 0..1 for the display
    std::array<std::atomic<float>, kMaxBands> bandEnv {};    // band input level (linear) for modulation triggers

private:
    float processStyle (int style, float x, StyleState& s, int c);
    void updateFilters();

    double sr = 48000.0;
    Settings set;
    std::array<Crossover, kMaxBands - 1> split;
    std::array<std::array<Crossover, kMaxBands - 1>, kMaxBands> comp;   // [band][split] phase compensation
    std::array<std::array<StyleState, 2>, kMaxBands> st;
    std::array<std::array<Svf2, 4>, kMaxBands> tone;                   // bass, mid, presence, treble
    std::array<Svf2, kMaxBands> fbBp;
    std::array<std::array<float, 2>, kMaxBands> fbY {}, env {}, dc {}, rmsIn {}, rmsOut {}, dynEnv {};
    Settings lastSet;
    bool filtersDirty = true;
};
} // namespace si::sat
