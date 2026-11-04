#include "Modules.h"
#include "dsp/FetComp.h"
#include <cmath>
#include <algorithm>

namespace si::rack
{
using namespace juce;
namespace
{
constexpr float kPi = 3.14159265358979f;
inline float dbg (float db) { return std::pow (10.0f, db * 0.05f); }
inline float todb (float g) { return 20.0f * std::log10 (std::max (1e-6f, g)); }

KnobDef K (const char* n, float lo, float hi, float def, const char* unit = "", bool lg = false) { KnobDef k; k.name = n; k.lo = lo; k.hi = hi; k.def = def; k.unit = unit; k.log = lg; return k; }
KnobDef S (const char* n, std::vector<const char*> names, std::vector<float> vals, int defIdx)
{
    KnobDef k; k.name = n; k.steps = std::move (names); k.stepValues = std::move (vals);
    k.lo = 0; k.hi = (float) k.steps.size() - 1; k.def = (float) defIdx;
    return k;
}

const std::vector<ModuleDef>& defs()
{
    static const std::vector<ModuleDef> d = {
        { "Empty", "", "", {}, {}, false, 0xff3a3a40 },
        { "EQ 73", "EQ", "British console EQ with stepped bands and transformer colour",
          { S ("HPF", { "Off", "50", "80", "160", "300" }, { 0, 50, 80, 160, 300 }, 0), S ("Low Freq", { "35", "60", "110", "220" }, { 35, 60, 110, 220 }, 1),
            K ("Low Gain", -16, 16, 0, "dB"), S ("Mid Freq", { "360", "700", "1.6k", "3.2k", "4.8k", "7.2k" }, { 360, 700, 1600, 3200, 4800, 7200 }, 3),
            K ("Mid Gain", -18, 18, 0, "dB"), K ("High 12k", -16, 16, 0, "dB") }, {}, false, 0xff5b6b8a },
        { "EQ P1A", "EQ", "Passive tube EQ - boost and cut the same low band for a tight, deep bottom",
          { S ("Low Freq", { "20", "30", "60", "100" }, { 20, 30, 60, 100 }, 2), K ("Low Boost", 0, 10, 0), K ("Low Atten", 0, 10, 0),
            S ("High Freq", { "3k", "4k", "5k", "8k", "10k", "12k", "16k" }, { 3000, 4000, 5000, 8000, 10000, 12000, 16000 }, 4),
            K ("High Boost", 0, 10, 0), K ("High Atten", 0, 10, 0) }, {}, false, 0xff8a6b4a },
        { "FET 76", "Dynamics", "The SI 76 FET compressor - Blue smooth, Black aggressive",
          { K ("Input", 0, 48, 18, "dB"), K ("Output", -24, 24, 0, "dB"), K ("Attack", 1, 7, 3), K ("Release", 1, 7, 5),
            S ("Ratio", { "4", "8", "12", "20", "All" }, { 0, 1, 2, 3, 4 }, 0), K ("Mix", 0, 100, 100, "%") }, { "Blue", "Black" }, true, 0xff2b2b30 },
        { "Opto 2A", "Dynamics", "Optical leveler - smooth, program-dependent, made for vocals and bass",
          { K ("Peak Reduct.", 0, 100, 30, "%"), K ("Gain", -10, 30, 0, "dB"), K ("HF Emphasis", 0, 100, 0, "%"), K ("Mix", 0, 100, 100, "%") },
          { "Compress", "Limit" }, true, 0xff7a7f86 },
        { "Bus VCA", "Dynamics", "Console bus compressor - glue for drums and the mix bus",
          { K ("Threshold", -30, 0, -10, "dB"), S ("Ratio", { "2", "4", "10" }, { 2, 4, 10 }, 1),
            S ("Attack", { "0.1", "0.3", "1", "3", "10", "30" }, { 0.1f, 0.3f, 1, 3, 10, 30 }, 3),
            S ("Release", { "0.1", "0.3", "0.6", "1.2", "Auto" }, { 0.1f, 0.3f, 0.6f, 1.2f, -1 }, 4), K ("Makeup", 0, 20, 0, "dB"), K ("Mix", 0, 100, 100, "%") },
          {}, true, 0xff3d4a5c },
        { "Console", "Color", "Console channel saturation and tone",
          { K ("Drive", 0, 100, 30, "%"), K ("Output", -12, 12, 0, "dB") }, { "Brit N", "US A", "Brit 4K", "Tube" }, false, 0xff6b4a5a },
        { "Exciter", "Color", "Shimmer adds air and sheen, Thickness adds low-end weight",
          { K ("Shimmer", 0, 100, 30, "%"), K ("Thickness", 0, 100, 20, "%"), K ("Output", -12, 12, 0, "dB") }, { "Clean", "Saturated" }, false, 0xff4a6b6b },
        { "Tape", "Color", "Tape machine: saturation, head bump, HF loss, wow",
          { K ("Drive", 0, 100, 30, "%"), K ("Bias", -50, 50, 0), K ("Wow", 0, 100, 10, "%"), K ("Output", -12, 12, 0, "dB") }, { "15 ips", "30 ips" }, false, 0xff6b5a3a },
        { "De-Esser", "Dynamics", "Tames harsh S and T sounds",
          { K ("Freq", 3000, 12000, 6500, "Hz", true), K ("Threshold", -50, 0, -24, "dB"), K ("Range", 0, 20, 8, "dB") }, { "Split", "Wide" }, true, 0xff5a4a6b },
        { "Gate", "Dynamics", "Noise gate / expander",
          { K ("Threshold", -80, 0, -45, "dB"), K ("Range", 0, 80, 40, "dB"), K ("Attack", 0.1f, 50, 1, "ms", true), K ("Hold", 0, 500, 40, "ms"),
            K ("Release", 5, 2000, 150, "ms", true) }, {}, true, 0xff3a5a4a },
        { "Transient", "Dynamics", "Shape the attack and the sustain",
          { K ("Attack", -100, 100, 0, "%"), K ("Sustain", -100, 100, 0, "%"), K ("Output", -12, 12, 0, "dB"), K ("Mix", 0, 100, 100, "%") }, {}, false, 0xff6b3a3a },
        { "Limiter", "Dynamics", "Brickwall-style peak limiter",
          { K ("Gain", 0, 24, 0, "dB"), K ("Ceiling", -12, 0, -1, "dB"), K ("Release", 10, 1000, 100, "ms", true) }, { "Clean", "Saturate" }, true, 0xff4a4a4a },
        { "Width", "Utility", "Stereo width with mono bass",
          { K ("Width", 0, 200, 100, "%"), S ("Bass Mono", { "Off", "80", "120", "200", "300" }, { 0, 80, 120, 200, 300 }, 0), K ("Output", -12, 12, 0, "dB") },
          {}, false, 0xff3a4a6b },
    };
    return d;
}

/** Minimal stereo TPT SVF (Cytomic form). */
struct Svf
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1.4f, m0 = 1, m1 = 0, m2 = 0, s1[2] {}, s2[2] {};
    void set (float g, float kk, float mm0, float mm1, float mm2) { k = kk; m0 = mm0; m1 = mm1; m2 = mm2; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    float tick (float x, int c)
    {
        const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3;
        s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c];
        return m0 * x + m1 * v1 + m2 * v2;
    }
    void reset() { s1[0] = s1[1] = s2[0] = s2[1] = 0; }
    static float G (float f, float sr) { return std::tan (kPi * std::min (f, sr * 0.45f) / sr); }
    void lp (float f, float q, float sr) { set (G (f, sr), 1.0f / q, 0, 0, 1); }
    void hp (float f, float q, float sr) { set (G (f, sr), 1.0f / q, 1, -1.0f / q, -1); }
    void bell (float f, float db, float q, float sr) { const float A = std::pow (10.0f, db / 40.0f), kk = 1.0f / (q * A); set (G (f, sr), kk, 1, kk * (A * A - 1), 0); }
    void lowShelf (float f, float db, float q, float sr) { const float A = std::pow (10.0f, db / 40.0f), kk = 1.0f / q; set (G (f, sr) / std::sqrt (A), kk, 1, kk * (A - 1), A * A - 1); }
    void highShelf (float f, float db, float q, float sr) { const float A = std::pow (10.0f, db / 40.0f), kk = 1.0f / q; set (G (f, sr) * std::sqrt (A), kk, A * A, kk * (1 - A) * A, 1 - A * A); }
    void bypass() { set (0.1f, 1.0f, 1, 0, 0); }
};

inline float rv (int type, int i, const std::array<float, kParams>& kn) { return knobReal (type, i, kn[(size_t) i]); }

//------------------------------------------------------------------------------ EQ 73
struct Eq73 : Processor
{
    float sr = 48000; Svf hpf, low, mid, high; std::array<float, kParams> last {}; bool dirty = true;
    void prepare (double s) override { sr = (float) s; dirty = true; reset(); }
    void reset() override { hpf.reset(); low.reset(); mid.reset(); high.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        if (dirty || k != last)
        {
            const float hp = rv (EQ73, 0, k);
            if (hp > 0) hpf.hp (hp, 0.7f, sr); else hpf.bypass();
            low.lowShelf (rv (EQ73, 1, k), rv (EQ73, 2, k), 0.55f, sr);
            mid.bell (rv (EQ73, 3, k), rv (EQ73, 4, k), 0.9f, sr);
            high.highShelf (12000.0f, rv (EQ73, 5, k), 0.6f, sr);
            last = k; dirty = false;
        }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                float y = high.tick (mid.tick (low.tick (hpf.tick (x, c), c), c), c);
                x = std::tanh (y * 1.08f + 0.02f) / 1.08f - std::tanh (0.02f) / 1.08f;   // transformer colour
            }
    }
};

//------------------------------------------------------------------------------ EQ P1A
struct EqP1A : Processor
{
    float sr = 48000; Svf lb, la, hb, ha; std::array<float, kParams> last {}; bool dirty = true;
    void prepare (double s) override { sr = (float) s; dirty = true; reset(); }
    void reset() override { lb.reset(); la.reset(); hb.reset(); ha.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        if (dirty || k != last)
        {
            const float f = rv (EQP1A, 0, k);
            lb.lowShelf (f, rv (EQP1A, 1, k) * 1.4f, 0.5f, sr);
            la.lowShelf (f * 1.6f, -rv (EQP1A, 2, k) * 1.75f, 0.7f, sr);   // the famous boost + cut "trick"
            hb.bell (rv (EQP1A, 3, k), rv (EQP1A, 4, k) * 1.6f, 0.65f, sr);
            ha.highShelf (10000.0f, -rv (EQP1A, 5, k) * 1.6f, 0.7f, sr);
            last = k; dirty = false;
        }
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                const float y = ha.tick (hb.tick (la.tick (lb.tick (x, c), c), c), c);
                x = (std::tanh (y * 1.1f + 0.05f) - std::tanh (0.05f)) / 1.1f;     // tube make-up stage
            }
    }
};

//------------------------------------------------------------------------------ FET 76
struct Fet : Processor
{
    FetComp comp;
    void prepare (double s) override { comp.prepare (s); }
    void reset() override { comp.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        comp.inputDb = rv (FET76, 0, k); comp.outputDb = rv (FET76, 1, k);
        comp.attackKnob = rv (FET76, 2, k); comp.releaseKnob = rv (FET76, 3, k);
        comp.ratio = (int) rv (FET76, 4, k); comp.mix = rv (FET76, 5, k) * 0.01f;
        comp.model = mode; comp.color = mode == 1 ? 0.5f : 0.3f; comp.link = true;
        comp.keyL = keyL; comp.keyR = keyR;
        grDb = comp.process (l, r, n, 2);
    }
};

//------------------------------------------------------------------------------ Opto 2A
struct Opto : Processor
{
    float sr = 48000, env = 0, fastRel = 0, slowRel = 0, held = 0, hpz[2] {};
    void prepare (double s) override { sr = (float) s; reset(); }
    void reset() override { env = fastRel = slowRel = held = 0; hpz[0] = hpz[1] = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        const float peak = rv (Opto2A, 0, k) * 0.01f, gain = dbg (rv (Opto2A, 1, k)), hf = rv (Opto2A, 2, k) * 0.01f, mix = rv (Opto2A, 3, k) * 0.01f;
        const float thr = -peak * 40.0f, ratio = mode == 1 ? 12.0f : 3.0f;
        const float atk = std::exp (-1.0f / (0.010f * sr));
        const float hpa = std::exp (-2.0f * kPi * 1500.0f / sr);
        float grMin = 0;
        for (int i = 0; i < n; ++i)
        {
            float det = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float x = keyL != nullptr ? (c ? keyR[i] : keyL[i]) : (c ? r[i] : l[i]);
                hpz[c] = x + hpa * (hpz[c] - x);
                det = std::max (det, std::abs (x + hf * 2.0f * (x - hpz[c])));
            }
            const float over = todb (det) - thr;
            const float target = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
            // opto cell: fast attack, release = 60 ms part + slow part that grows with how long it has been compressing
            if (target > env) env = target + atk * (env - target);
            else
            {
                held = std::clamp (held + 1.0f / (2.0f * sr) * (env > 1.0f ? 1.0f : -1.0f), 0.0f, 1.0f);
                const float relT = 0.06f + 1.5f * std::max (0.0f, held);
                env = target + std::exp (-1.0f / (relT * sr)) * (env - target);
            }
            const float g = dbg (-env) * gain;
            grMin = std::min (grMin, -env);
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                const float wet = std::tanh (x * g * 1.05f) / 1.05f;
                x = x * (1.0f - mix) + wet * mix;
            }
        }
        grDb = grMin;
    }
};

//------------------------------------------------------------------------------ Bus VCA
struct Bus : Processor
{
    float sr = 48000, env = 0, rms = 0, slow = 0;
    void prepare (double s) override { sr = (float) s; reset(); }
    void reset() override { env = rms = slow = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        const float thr = rv (BusVCA, 0, k), ratio = rv (BusVCA, 1, k), atkMs = rv (BusVCA, 2, k), rel = rv (BusVCA, 3, k);
        const float makeup = dbg (rv (BusVCA, 4, k)), mix = rv (BusVCA, 5, k) * 0.01f;
        const float aA = std::exp (-1.0f / (atkMs * 0.001f * sr)), rmsA = std::exp (-1.0f / (0.005f * sr));
        float grMin = 0;
        for (int i = 0; i < n; ++i)
        {
            const float* dl = keyL != nullptr ? keyL : l;
            const float* dr = keyL != nullptr ? keyR : r;
            const float x2 = 0.5f * (dl[i] * dl[i] + dr[i] * dr[i]);
            rms = x2 + rmsA * (rms - x2);
            const float over = todb (std::sqrt (rms)) - thr;
            const float target = over > 0 ? over * (1.0f - 1.0f / ratio) : 0.0f;
            slow += (target - slow) * (1.0f / (0.5f * sr));
            const float relT = rel > 0 ? rel : 0.1f + 1.0f * std::min (1.0f, slow / 6.0f);   // Auto: longer when it keeps working
            env = target > env ? target + aA * (env - target) : target + std::exp (-1.0f / (relT * sr)) * (env - target);
            const float g = dbg (-env) * makeup;
            grMin = std::min (grMin, -env);
            l[i] = l[i] * (1.0f - mix) + l[i] * g * mix;
            r[i] = r[i] * (1.0f - mix) + r[i] * g * mix;
        }
        grDb = grMin;
    }
};

//------------------------------------------------------------------------------ Console
struct ConsoleP : Processor
{
    float sr = 48000; Svf tone; int lastMode = -1;
    void prepare (double s) override { sr = (float) s; lastMode = -1; reset(); }
    void reset() override { tone.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        if (mode != lastMode)
        {
            if (mode == 0) tone.lowShelf (90, 1.0f, 0.6f, sr);
            else if (mode == 1) tone.bell (1200, 1.2f, 0.7f, sr);
            else if (mode == 2) tone.highShelf (9000, 1.2f, 0.6f, sr);
            else tone.lowShelf (200, 0.6f, 0.6f, sr);
            lastMode = mode;
        }
        const float drive = 1.0f + rv (Console, 0, k) * 0.06f, out = dbg (rv (Console, 1, k));
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                float y = tone.tick (x, c) * drive;
                switch (mode)
                {
                    case 0: y = std::tanh (y + 0.08f) - std::tanh (0.08f); break;          // N: soft, even
                    case 1: y = y / (1.0f + std::abs (y) * 0.6f) * 1.2f; break;             // A: punchy
                    case 2: y = std::tanh (y); break;                                        // 4K: clean, crisp
                    default: y = (std::tanh (y + 0.25f) - std::tanh (0.25f)) * 1.1f; break; // Tube: rich even
                }
                x = y / std::sqrt (drive) * out;
            }
    }
};

//------------------------------------------------------------------------------ Exciter
struct ExciterP : Processor
{
    float sr = 48000; Svf hp, lp;
    void prepare (double s) override { sr = (float) s; hp.hp (3500, 0.7f, sr); lp.lp (220, 0.7f, sr); reset(); }
    void reset() override { hp.reset(); lp.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        const float sh = rv (Exciter, 0, k) * 0.01f, th = rv (Exciter, 1, k) * 0.01f, out = dbg (rv (Exciter, 2, k));
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                const float hi = hp.tick (x, c), lo = lp.tick (x, c);
                float y = x + sh * 0.8f * std::tanh (hi * 4.0f) / 4.0f * 3.0f + th * 0.6f * (std::tanh (lo * 3.0f) / 3.0f * 2.0f);
                if (mode == 1) y = std::tanh (y * 1.3f) / 1.3f;
                x = y * out;
            }
    }
};

//------------------------------------------------------------------------------ Tape
struct TapeP : Processor
{
    float sr = 48000; Svf bump, loss; std::vector<float> dl[2]; int w = 0; double ph = 0; int lastMode = -1;
    void prepare (double s) override { sr = (float) s; for (auto& d : dl) d.assign ((size_t) (sr * 0.02) + 8, 0.0f); lastMode = -1; reset(); }
    void reset() override { bump.reset(); loss.reset(); for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f); w = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        if (mode != lastMode)
        {
            bump.lowShelf (mode == 0 ? 70.0f : 45.0f, mode == 0 ? 2.0f : 1.0f, 0.8f, sr);
            loss.lp (mode == 0 ? 15000.0f : 19000.0f, 0.6f, sr);
            lastMode = mode;
        }
        const float drive = 1.0f + rv (Tape, 0, k) * 0.05f, bias = rv (Tape, 1, k) * 0.004f, wow = rv (Tape, 2, k) * 0.01f, out = dbg (rv (Tape, 3, k));
        const int N = (int) dl[0].size();
        for (int i = 0; i < n; ++i)
        {
            ph += 0.7 / sr; if (ph >= 1) ph -= 1;
            const float d = 8.0f + wow * 0.0018f * sr * (1.0f + (float) std::sin (ph * 6.2831853));
            for (int c = 0; c < 2; ++c)
            {
                float& x = c ? r[i] : l[i];
                dl[c][(size_t) w] = x;
                const float pos = (float) w - d;
                const int i0 = (int) std::floor (pos);
                const float f = pos - (float) i0;
                const float a = dl[c][(size_t) ((i0 % N + N) % N)], b = dl[c][(size_t) (((i0 + 1) % N + N) % N)];
                float y = (wow > 0.001f ? a + (b - a) * f : x) * drive;
                y = std::atan (y + bias) - std::atan (bias);
                y = loss.tick (bump.tick (y, c), c);
                x = y / std::sqrt (drive) * out;
            }
            w = (w + 1) % N;
        }
    }
};

//------------------------------------------------------------------------------ De-Esser
struct DeEss : Processor
{
    float sr = 48000, env = 0; Svf split; float lastF = -1;
    void prepare (double s) override { sr = (float) s; lastF = -1; reset(); }
    void reset() override { split.reset(); env = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        const float f = rv (DeEsser, 0, k), thr = rv (DeEsser, 1, k), range = rv (DeEsser, 2, k);
        if (f != lastF) { split.hp (f, 0.7f, sr); lastF = f; }
        const float a = std::exp (-1.0f / (0.0005f * sr)), rel = std::exp (-1.0f / (0.06f * sr));
        float grMin = 0;
        for (int i = 0; i < n; ++i)
        {
            const float hl = split.tick (l[i], 0), hr = split.tick (r[i], 1);
            const float lv = std::max (std::abs (hl), std::abs (hr));
            env = lv > env ? lv + a * (env - lv) : lv + rel * (env - lv);
            const float gr = -std::min (range, std::max (0.0f, todb (env) - thr) * 0.7f);
            grMin = std::min (grMin, gr);
            const float g = dbg (gr);
            if (mode == 0) { l[i] += hl * (g - 1.0f); r[i] += hr * (g - 1.0f); }
            else { l[i] *= g; r[i] *= g; }
        }
        grDb = grMin;
    }
};

//------------------------------------------------------------------------------ Gate
struct GateP : Processor
{
    float sr = 48000, env = 0, gain = 1, holdLeft = 0;
    void prepare (double s) override { sr = (float) s; reset(); }
    void reset() override { env = 0; gain = 1; holdLeft = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        const float thr = rv (Gate, 0, k), range = dbg (-rv (Gate, 1, k));
        const float att = std::exp (-1.0f / (rv (Gate, 2, k) * 0.001f * sr)), rel = std::exp (-1.0f / (rv (Gate, 4, k) * 0.001f * sr));
        const float holdS = rv (Gate, 3, k) * 0.001f * sr;
        const float er = std::exp (-1.0f / (0.01f * sr));
        float grMin = 0;
        for (int i = 0; i < n; ++i)
        {
            const float lv = keyL != nullptr ? std::max (std::abs (keyL[i]), std::abs (keyR[i])) : std::max (std::abs (l[i]), std::abs (r[i]));
            env = std::max (lv, env * er);
            const bool open = todb (env) > thr;
            if (open) holdLeft = holdS; else holdLeft -= 1.0f;
            const float target = (open || holdLeft > 0) ? 1.0f : range;
            gain = target > gain ? target + att * (gain - target) : target + rel * (gain - target);
            l[i] *= gain; r[i] *= gain;
            grMin = std::min (grMin, todb (gain));
        }
        grDb = grMin;
    }
};

//------------------------------------------------------------------------------ Transient
struct TransientP : Processor
{
    float sr = 48000, fast = 0, slow = 0;
    void prepare (double s) override { sr = (float) s; reset(); }
    void reset() override { fast = slow = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        const float atk = rv (Transient, 0, k) * 0.01f, sus = rv (Transient, 1, k) * 0.01f, out = dbg (rv (Transient, 2, k)), mix = rv (Transient, 3, k) * 0.01f;
        const float fa = std::exp (-1.0f / (0.001f * sr)), fr = std::exp (-1.0f / (0.03f * sr));
        const float sa = std::exp (-1.0f / (0.03f * sr)), srl = std::exp (-1.0f / (0.25f * sr));
        for (int i = 0; i < n; ++i)
        {
            const float lv = std::max (std::abs (l[i]), std::abs (r[i])) + 1e-6f;
            fast = lv > fast ? lv + fa * (fast - lv) : lv + fr * (fast - lv);
            slow = lv > slow ? lv + sa * (slow - lv) : lv + srl * (slow - lv);
            const float ratio = std::clamp (fast / (slow + 1e-6f), 0.25f, 4.0f);
            const float g = std::clamp (std::pow (ratio, atk * 1.5f) * std::pow (1.0f / ratio, sus * 1.0f), 0.1f, 6.0f);
            l[i] = (l[i] * (1.0f - mix) + l[i] * g * mix) * out;
            r[i] = (r[i] * (1.0f - mix) + r[i] * g * mix) * out;
        }
    }
};

//------------------------------------------------------------------------------ Limiter
struct LimiterP : Processor
{
    float sr = 48000, env = 0;
    void prepare (double s) override { sr = (float) s; reset(); }
    void reset() override { env = 0; }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int mode) override
    {
        const float in = dbg (rv (Limiter, 0, k)), ceil = dbg (rv (Limiter, 1, k));
        const float rel = std::exp (-1.0f / (rv (Limiter, 2, k) * 0.001f * sr));
        float grMin = 0;
        for (int i = 0; i < n; ++i)
        {
            float a = l[i] * in, b = r[i] * in;
            const float pk = std::max (std::abs (a), std::abs (b));
            const float need = pk > ceil ? ceil / pk : 1.0f;
            env = need < env || env == 0 ? need : need + rel * (env - need);
            a *= env; b *= env;
            if (mode == 1) { a = std::tanh (a / ceil) * ceil; b = std::tanh (b / ceil) * ceil; }
            else { a = std::clamp (a, -ceil, ceil); b = std::clamp (b, -ceil, ceil); }
            l[i] = a; r[i] = b;
            grMin = std::min (grMin, todb (env));
        }
        grDb = grMin;
    }
};

//------------------------------------------------------------------------------ Width
struct WidthP : Processor
{
    float sr = 48000; Svf lp; float lastF = -1;
    void prepare (double s) override { sr = (float) s; lastF = -1; reset(); }
    void reset() override { lp.reset(); }
    void process (float* l, float* r, int n, const std::array<float, kParams>& k, int) override
    {
        const float w = rv (Width, 0, k) * 0.01f, mono = rv (Width, 1, k), out = dbg (rv (Width, 2, k));
        if (mono != lastF) { if (mono > 0) lp.lp (mono, 0.7f, sr); lastF = mono; }
        for (int i = 0; i < n; ++i)
        {
            const float m = 0.5f * (l[i] + r[i]);
            float s = 0.5f * (l[i] - r[i]) * w;
            if (mono > 0) s -= lp.tick (s, 0);   // remove side below the mono frequency
            l[i] = (m + s) * out;
            r[i] = (m - s) * out;
        }
    }
};
} // namespace

const ModuleDef& moduleDef (int type) { return defs()[(size_t) std::clamp (type, 0, (int) defs().size() - 1)]; }

float knobReal (int type, int idx, float norm)
{
    const auto& k = moduleDef (type).knobs[(size_t) idx];
    norm = std::clamp (norm, 0.0f, 1.0f);
    if (! k.steps.empty()) return k.stepValues[(size_t) std::lround (norm * (float) (k.steps.size() - 1))];
    if (k.log) return k.lo * std::pow (k.hi / k.lo, norm);
    return k.lo + (k.hi - k.lo) * norm;
}

float knobNorm (int type, int idx, float real)
{
    const auto& k = moduleDef (type).knobs[(size_t) idx];
    if (k.name == nullptr) return 0.0f;
    if (! k.steps.empty())
    {
        for (size_t i = 0; i < k.stepValues.size(); ++i)
            if (std::abs (k.stepValues[i] - real) < 1e-4f) return (float) i / (float) std::max<size_t> (1, k.steps.size() - 1);
        return std::clamp (real / std::max (1.0f, (float) (k.steps.size() - 1)), 0.0f, 1.0f);   // real given as step index
    }
    if (k.log) return std::clamp (std::log (real / k.lo) / std::log (k.hi / k.lo), 0.0f, 1.0f);
    return std::clamp ((real - k.lo) / (k.hi - k.lo), 0.0f, 1.0f);
}

String knobText (int type, int idx, float norm)
{
    const auto& k = moduleDef (type).knobs[(size_t) idx];
    if (k.name == nullptr) return {};
    if (! k.steps.empty()) return k.steps[(size_t) std::lround (std::clamp (norm, 0.0f, 1.0f) * (float) (k.steps.size() - 1))];
    const float v = knobReal (type, idx, norm);
    String unit (k.unit);
    if (unit == "Hz" && v >= 1000.0f) return String (v / 1000.0f, 1) + " kHz";
    const bool ints = unit == "%" || unit == "Hz" || std::abs (k.hi - k.lo) > 60.0f;
    return (ints ? String (std::lround (v)) : String (v, 1)) + (unit.isNotEmpty() ? " " + unit : String());
}

std::unique_ptr<Processor> createProcessor (int type)
{
    switch (type)
    {
        case EQ73: return std::make_unique<Eq73>();
        case EQP1A: return std::make_unique<EqP1A>();
        case FET76: return std::make_unique<Fet>();
        case Opto2A: return std::make_unique<Opto>();
        case BusVCA: return std::make_unique<Bus>();
        case Console: return std::make_unique<ConsoleP>();
        case Exciter: return std::make_unique<ExciterP>();
        case Tape: return std::make_unique<TapeP>();
        case DeEsser: return std::make_unique<DeEss>();
        case Gate: return std::make_unique<GateP>();
        case Transient: return std::make_unique<TransientP>();
        case Limiter: return std::make_unique<LimiterP>();
        case Width: return std::make_unique<WidthP>();
        default: return nullptr;
    }
}
} // namespace si::rack

namespace si::rack
{
float knobDefaultNorm (int type, int idx)
{
    const auto& k = moduleDef (type).knobs[(size_t) idx];
    if (k.name == nullptr) return 0.0f;
    if (! k.steps.empty()) return k.def / (float) std::max<size_t> (1, k.steps.size() - 1);
    return knobNorm (type, idx, k.def);
}
} // namespace si::rack
