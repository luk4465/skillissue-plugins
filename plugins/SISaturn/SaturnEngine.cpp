#include "SaturnEngine.h"
#include <algorithm>

namespace si::sat
{
namespace
{
constexpr float kPi = 3.14159265358979f;
inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float sgn (float x) { return x < 0.0f ? -1.0f : 1.0f; }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float onePole (float x, float& z, float a) { z = x + a * (z - x); return z; }

struct StyleInfo { const char* name; const char* cat; };
const StyleInfo kStyles[kNumStyles] = {
    { "Clean Tube", "Tube" }, { "Warm Tube", "Tube" }, { "Saturated Tube", "Tube" }, { "Heavy Tube", "Tube" }, { "Broken Tube", "Tube" },
    { "Subtle Tape", "Tape" }, { "Warm Tape", "Tape" }, { "Saturated Tape", "Tape" }, { "Heavy Tape", "Tape" },
    { "Gentle Saturation", "Saturation" }, { "Warm Saturation", "Saturation" }, { "Heavy Saturation", "Saturation" },
    { "Clean Amp", "Amp" }, { "Warm Amp", "Amp" }, { "Crunchy Amp", "Amp" }, { "Breakup Amp", "Amp" },
    { "Smooth Lead", "Amp" }, { "Heavy Lead", "Amp" }, { "American Amp", "Amp" }, { "British Amp", "Amp" },
    { "Gentle Transformer", "Transformer" }, { "Warm Transformer", "Transformer" }, { "Lush Transformer", "Transformer" },
    { "Smudge", "FX" }, { "Breakdown", "FX" }, { "Foldback", "FX" }, { "Rectify", "FX" }, { "Destroy", "FX" },
};
const float kTube[5][2] = { { 1.0f, 0.05f }, { 1.3f, 0.2f }, { 1.9f, 0.25f }, { 3.0f, 0.35f }, { 3.5f, 0.45f } };
const float kTape[4] = { 0.6f, 1.0f, 1.7f, 2.8f };
const float kSat[3] = { 0.8f, 1.2f, 2.0f };
// amp: stage-1 gain, stage-2 gain, mid scoop (+) / push (-), cab corner
const float kAmp[8][4] = { { 1.5f, 1.0f, 0.0f, 6500 }, { 2.5f, 1.5f, 0.2f, 5000 }, { 4.0f, 3.0f, 0.3f, 5500 }, { 6.0f, 4.0f, 0.2f, 5000 },
                           { 10.0f, 6.0f, 0.1f, 4500 }, { 16.0f, 10.0f, 0.4f, 4000 }, { 3.0f, 2.0f, 0.45f, 6000 }, { 5.0f, 3.0f, -0.25f, 5000 } };
const float kTrans[3][2] = { { 1.5f, 0.05f }, { 3.0f, 0.10f }, { 5.0f, 0.16f } };

float fold (float x)
{
    float y = clampf (x, -8.0f, 8.0f);
    for (int i = 0; i < 8 && std::abs (y) > 1.0f; ++i) y = sgn (y) * 2.0f - y;
    return y;
}
} // namespace

const char* styleName (int s) { return kStyles[std::min (kNumStyles - 1, std::max (0, s))].name; }
const char* styleCategory (int s) { return kStyles[std::min (kNumStyles - 1, std::max (0, s))].cat; }

float shape (int s, float x)
{
    if (s <= 4) { const float h = kTube[s][0], b = kTube[s][1]; float y = std::tanh (h * x + b) - std::tanh (b); if (s == 4 && std::abs (x) < 0.06f) y *= 0.3f; return y; }
    if (s <= 8) { const float k = kTape[s - 5]; return std::atan (k * x) / std::atan (k) * 0.9f; }
    if (s <= 11) { const float t = clampf (kSat[s - 9] * x + (s == 10 ? 0.08f : 0.0f), -1.0f, 1.0f); return 1.5f * t - 0.5f * t * t * t; }
    if (s <= 19) { const auto& a = kAmp[s - 12]; return std::tanh (a[1] * std::tanh (a[0] * x) + 0.1f) * 0.85f; }
    if (s <= 22) { const auto& t = kTrans[s - 20]; const float c = clampf (x, -1.4f, 1.4f); return std::tanh ((c - t[1] * c * c * c) * (1.0f + t[0] * 0.1f)); }
    switch (s)
    {
        case 23: return std::tanh (x * 1.4f);
        case 24: return std::tanh (2.0f * x);
        case 25: return fold (x);
        case 26: return std::tanh (std::abs (x) * 0.8f + x * 0.2f);
        case 27: { const float q = 1.0f / 32.0f; return clampf (std::round (x / q) * q, -1.0f, 1.0f); }
        default: return x;
    }
}

//============================================================================== crossover
void Crossover::set (float fc, float sr, int ord)
{
    order = std::min (3, std::max (0, ord));
    const float g = std::tan (kPi * clampf (fc, 10.0f, sr * 0.45f) / sr);
    G1 = g / (1.0f + g);
    if (order == 2) { lo[0].lp (g, 0.7071f); lo[1].lp (g, 0.7071f); hi[0].hp (g, 0.7071f); hi[1].hp (g, 0.7071f); }
    if (order == 3)
    {
        const float q[4] = { 0.5412f, 1.3066f, 0.5412f, 1.3066f };
        for (int i = 0; i < 4; ++i) { lo[(size_t) i].lp (g, q[i]); hi[(size_t) i].hp (g, q[i]); }
    }
}

void Crossover::process (float x, int c, float& l, float& h)
{
    auto lp1 = [&] (float in, float* zz) { const float v = (in - zz[c]) * G1; const float y = v + zz[c]; zz[c] = y + v; return y; };
    switch (order)
    {
        case 0: l = lp1 (x, z[0]); h = x - l; break;                                         // 6 dB: complementary
        case 1:
        {
            l = lp1 (lp1 (x, z[0]), z[1]);                                                  // LR2 (high path inverted)
            const float h1 = x - lp1 (x, z[2]);
            h = -(h1 - lp1 (h1, z[3]));
            break;
        }
        case 2: l = lo[1].tick (lo[0].tick (x, c), c); h = hi[1].tick (hi[0].tick (x, c), c); break;   // LR4
        default:
            l = lo[3].tick (lo[2].tick (lo[1].tick (lo[0].tick (x, c), c), c), c);          // LR8
            h = hi[3].tick (hi[2].tick (hi[1].tick (hi[0].tick (x, c), c), c), c);
            break;
    }
}

void Crossover::reset()
{
    for (auto& s : lo) s.reset();
    for (auto& s : hi) s.reset();
    for (auto& zz : z) zz[0] = zz[1] = 0.0f;
}

//============================================================================== engine
void SaturnEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& b : st)
        for (auto& s : b) s.dly.assign (4096, 0.0f);
    filtersDirty = true;
    reset();
}

void SaturnEngine::reset()
{
    for (auto& s : split) s.reset();
    for (auto& b : comp) for (auto& s : b) s.reset();
    for (int b = 0; b < kMaxBands; ++b)
    {
        for (auto& t : tone[(size_t) b]) t.reset();
        fbBp[(size_t) b].reset();
        for (int c = 0; c < 2; ++c)
        {
            auto& s = st[(size_t) b][(size_t) c];
            std::fill (std::begin (s.z), std::end (s.z), 0.0f);
            std::fill (s.dly.begin(), s.dly.end(), 0.0f);
            s.dw = s.hold = 0;
            s.held = s.rnd = 0.0f;
            s.flip = s.lastSign = 1.0f;
            fbY[(size_t) b][(size_t) c] = env[(size_t) b][(size_t) c] = dc[(size_t) b][(size_t) c] = dynEnv[(size_t) b][(size_t) c] = 0.0f;
            rmsIn[(size_t) b][(size_t) c] = rmsOut[(size_t) b][(size_t) c] = 1e-6f;
        }
    }
}

void SaturnEngine::updateFilters()
{
    const float fs = (float) sr;
    const int nb = std::min (kMaxBands, std::max (1, set.bands));
    auto xs = set.xo;
    std::sort (xs.begin(), xs.begin() + std::max (1, nb - 1));
    for (int i = 0; i < kMaxBands - 1; ++i)
    {
        split[(size_t) i].set (xs[(size_t) i], fs, set.slope[(size_t) i]);
        for (int b = 0; b < kMaxBands; ++b) comp[(size_t) b][(size_t) i].set (xs[(size_t) i], fs, set.slope[(size_t) i]);
    }
    auto shelf = [fs] (Svf2& f, float freq, float db, bool high)
    {
        const float A = std::pow (10.0f, db / 40.0f), t = std::tan (kPi * freq / fs), k = 1.0f / 0.71f;
        if (high) f.set (t * std::sqrt (A), k, A * A, k * (1.0f - A) * A, 1.0f - A * A);
        else f.set (t / std::sqrt (A), k, 1.0f, k * (A - 1.0f), A * A - 1.0f);
    };
    auto bell = [fs] (Svf2& f, float freq, float db, float q)
    {
        const float A = std::pow (10.0f, db / 40.0f), k = 1.0f / (q * A);
        f.set (std::tan (kPi * freq / fs), k, 1.0f, k * (A * A - 1.0f), 0.0f);
    };
    for (int b = 0; b < kMaxBands; ++b)
    {
        const auto& bs = set.band[(size_t) b];
        shelf (tone[(size_t) b][0], 120.0f, bs.bass, false);
        bell (tone[(size_t) b][1], 900.0f, bs.mid, 0.7f);
        bell (tone[(size_t) b][2], 3500.0f, bs.presence, 1.0f);
        shelf (tone[(size_t) b][3], std::min (7000.0f, fs * 0.4f), bs.treble, true);
        const float q = 4.0f, g = std::tan (kPi * clampf (bs.fbFreq, 20.0f, fs * 0.45f) / fs);
        fbBp[(size_t) b].set (g, 1.0f / q, 0.0f, 1.0f / q, 0.0f);
    }
}

float SaturnEngine::processStyle (int s, float x, StyleState& S, int)
{
    const float fs = (float) sr;
    if (s <= 4) return shape (s, x);
    if (s <= 8)
    {
        // tape: pre-emphasis -> saturation -> de-emphasis + HF loss
        const float k = kTape[s - 5];
        const float a = std::exp (-2.0f * kPi * 2500.0f / fs);
        const float hp = x - onePole (x, S.z[0], a);
        float y = std::atan (k * (x + 0.6f * hp)) / std::atan (k) * 0.9f;
        y -= 0.375f * (y - onePole (y, S.z[1], a));
        return onePole (y, S.z[2], std::exp (-2.0f * kPi * (17000.0f - 2500.0f * k) / fs));
    }
    if (s <= 11) return shape (s, x);
    if (s <= 19)
    {
        const auto& p = kAmp[s - 12];
        const float hp = x - onePole (x, S.z[0], std::exp (-2.0f * kPi * 90.0f / fs));
        float y = std::tanh (p[0] * hp);
        // mid scoop / push around 800 Hz
        const float lo = onePole (y, S.z[1], std::exp (-2.0f * kPi * 500.0f / fs));
        const float hi = y - onePole (y, S.z[2], std::exp (-2.0f * kPi * 1600.0f / fs));
        const float midPart = y - lo - hi;
        y = lo + hi + midPart * (1.0f - p[2]);
        y = std::tanh (p[1] * y + 0.1f) - std::tanh (0.1f);
        // cabinet: two poles
        const float ca = std::exp (-2.0f * kPi * p[3] / fs);
        return onePole (onePole (y, S.z[3], ca), S.z[4], ca) * 0.9f;
    }
    if (s <= 22)
    {
        const auto& t = kTrans[s - 20];
        const float low = onePole (x, S.z[0], std::exp (-2.0f * kPi * 150.0f / fs));
        const float c = clampf (x, -1.4f, 1.4f);
        const float y = c - t[1] * c * c * c + (std::tanh (low * t[0]) / std::max (1.0f, t[0] * 0.6f) - low) * 0.5f;
        return std::tanh (y);
    }
    switch (s)
    {
        case 23:   // Smudge: wandering short delay smears the transient detail
        {
            const int N = (int) S.dly.size();
            S.dly[(size_t) S.dw] = x;
            if (++S.hold >= 64)
            {
                S.hold = 0;
                S.seed = S.seed * 1664525u + 1013904223u;
                S.held = (float) ((S.seed >> 8) & 0xFFFF) / 65535.0f;
            }
            S.rnd += 0.002f * (S.held - S.rnd);
            const float d = 20.0f + S.rnd * 0.006f * fs;
            const float pos = (float) S.dw - d;
            const int i0 = (int) std::floor (pos);
            const float fr = pos - (float) i0;
            const float a = S.dly[(size_t) ((i0 % N + N) % N)], b = S.dly[(size_t) (((i0 + 1) % N + N) % N)];
            S.dw = (S.dw + 1) % N;
            return std::tanh (1.4f * (0.5f * x + 0.7f * (a + (b - a) * fr)));
        }
        case 24:   // Breakdown: octave-down divider + grit
        {
            const float lpx = onePole (x, S.z[0], std::exp (-2.0f * kPi * 400.0f / fs));
            const float sg = sgn (lpx);
            if (sg > 0.0f && S.lastSign < 0.0f) S.flip = -S.flip;
            S.lastSign = sg;
            const float sub = S.flip * std::abs (lpx);
            return std::tanh (2.0f * (0.4f * x + sub));
        }
        case 25: return fold (x);
        case 26: return std::tanh (std::abs (x) * 0.8f + x * 0.2f);
        case 27:   // Destroy: rate + bit reduction + clip
        {
            const int holdN = std::max (1, (int) (fs / 12000.0f));
            if (S.hold++ % holdN == 0) S.held = x;
            const float q = 1.0f / 32.0f;
            return clampf (std::round (S.held / q) * q, -1.0f, 1.0f);
        }
        default: return x;
    }
}

void SaturnEngine::process (float* l, float* r, int n)
{
    const float fs = (float) sr;
    const int nb = std::min (kMaxBands, std::max (1, set.bands));

    // filters follow tone / crossover / feedback-frequency changes
    bool changed = filtersDirty || set.bands != lastSet.bands;
    for (int i = 0; i < kMaxBands - 1 && ! changed; ++i)
        changed = set.xo[(size_t) i] != lastSet.xo[(size_t) i] || set.slope[(size_t) i] != lastSet.slope[(size_t) i];
    for (int b = 0; b < kMaxBands && ! changed; ++b)
    {
        const auto& a = set.band[(size_t) b];
        const auto& o = lastSet.band[(size_t) b];
        changed = a.bass != o.bass || a.mid != o.mid || a.presence != o.presence || a.treble != o.treble || a.fbFreq != o.fbFreq;
    }
    if (changed) { updateFilters(); lastSet = set; filtersDirty = false; }

    bool anySolo = false;
    for (int b = 0; b < nb; ++b) anySolo |= set.band[(size_t) b].solo;

    const float envA = std::exp (-1.0f / (0.002f * fs)), envR = std::exp (-1.0f / (0.08f * fs));
    const float rmsA = std::exp (-1.0f / (0.3f * fs));
    const float dcA = std::exp (-2.0f * kPi * 10.0f / fs);
    float act[kMaxBands] {}, benv[kMaxBands] {};

    for (int i = 0; i < n; ++i)
    {
        float in[2] = { l[i], r[i] };
        if (set.midSide) { const float m = 0.5f * (in[0] + in[1]), s = 0.5f * (in[0] - in[1]); in[0] = m; in[1] = s; }
        float out[2] {}, dry[2] {};
        for (int c = 0; c < 2; ++c)
        {
            float bandIn[kMaxBands] {};
            float rest = in[c];
            for (int sIdx = 0; sIdx < nb - 1; ++sIdx)
            {
                float lo, hi;
                split[(size_t) sIdx].process (rest, c, lo, hi);
                // the band below this split still needs the phase of every later split
                for (int k = sIdx + 1; k < nb - 1; ++k) lo = comp[(size_t) sIdx][(size_t) k].allpass (lo, c);
                bandIn[sIdx] = lo;
                rest = hi;
            }
            bandIn[nb - 1] = rest;

            for (int b = 0; b < nb; ++b)
            {
                const auto& bs = set.band[(size_t) b];
                const float x = bandIn[b];
                dry[c] += x;
                float& e = env[(size_t) b][(size_t) c];
                const float ax = std::abs (x);
                e = ax > e ? ax + envA * (e - ax) : ax + envR * (e - ax);
                benv[b] = std::max (benv[b], e);
                const bool silent = bs.mute || (anySolo && ! bs.solo);
                if (! bs.on) { out[c] += silent ? 0.0f : x; continue; }

                // resonant feedback (fades out on silence so it never howls on its own)
                const float fbAmt = bs.feedback * 0.95f * clampf (e * 100.0f, 0.0f, 1.0f);
                const float xf = x + fbAmt * fbY[(size_t) b][(size_t) c];

                const float pan = bs.drivePan * (c == 0 ? -12.0f : 12.0f);
                const float dg = dbToGain (clampf (bs.driveDb + pan, -24.0f, 48.0f));
                float y = processStyle (bs.style, xf * dg, st[(size_t) b][(size_t) c], c);
                if (! set.autoGain) y /= std::sqrt (std::max (1.0f, dg));

                float& d = dc[(size_t) b][(size_t) c];
                d = y + dcA * (d - y);
                y -= d;
                fbY[(size_t) b][(size_t) c] = fbBp[(size_t) b].tick (y, c);

                for (auto& t : tone[(size_t) b]) y = t.tick (y, c);

                if (set.autoGain)
                {
                    float& ri = rmsIn[(size_t) b][(size_t) c];
                    float& ro = rmsOut[(size_t) b][(size_t) c];
                    ri = x * x + rmsA * (ri - x * x);
                    ro = y * y + rmsA * (ro - y * y);
                    y *= clampf (std::sqrt ((ri + 1e-9f) / (ro + 1e-9f)), 0.03f, 4.0f);
                }

                // dynamics: + pumps (compresses after loud hits), - gates the quiet parts
                if (bs.dyn != 0.0f)
                {
                    const float envDb = 20.0f * std::log10 (std::max (1e-6f, e));
                    const float gdb = bs.dyn > 0.0f ? -bs.dyn * 18.0f * clampf ((envDb + 36.0f) / 30.0f, 0.0f, 1.0f)
                                                    : bs.dyn * 30.0f * clampf ((-30.0f - envDb) / 30.0f, 0.0f, 1.0f);
                    float& de = dynEnv[(size_t) b][(size_t) c];
                    de += (gdb - de) * 0.002f;
                    y *= dbToGain (de);
                }

                y = x + (y - x) * bs.mix;
                y *= dbToGain (bs.levelDb + bs.levelPan * (c == 0 ? -6.0f : 6.0f));
                if (silent) y = 0.0f;
                out[c] += y;
                act[b] = std::max (act[b], clampf ((20.0f * std::log10 (std::max (1e-6f, e)) + 50.0f) / 50.0f, 0.0f, 1.0f)
                                                * clampf (bs.driveDb / 24.0f + 0.25f, 0.0f, 1.0f));
            }
        }
        float o0 = dry[0] + (out[0] - dry[0]) * set.mix, o1 = dry[1] + (out[1] - dry[1]) * set.mix;
        if (set.midSide) { const float L = o0 + o1, R = o0 - o1; o0 = L; o1 = R; }
        l[i] = o0;
        r[i] = o1;
    }
    for (int b = 0; b < kMaxBands; ++b)
    {
        activity[(size_t) b].store (b < nb ? act[b] : 0.0f);
        bandEnv[(size_t) b].store (b < nb ? benv[b] : 0.0f);
    }
}
} // namespace si::sat
