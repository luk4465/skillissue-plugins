#include "FetComp.h"
#include <vector>

using namespace juce;

/*  1176-style FET compressor, matched to measurements of a reference 1176 emulation (Blue = rev. "Bluey",
    Black = rev. "Blacky"), taken with SITests --comp (1 kHz sine, -40..0 dBFS, settled; see tests/PluginMeasure.cpp):

    - Input / Output follow the measured knob laws of the dials (not a plain 1 dB per dB scale).
    - The static curve is table driven: for every model and ratio, the gain reduction as a function of the level
      after the Input stage (expressed at Output 24). Rows measured at Input 24 / 36 / 48 collapse onto one curve.
    - Detection is peak based; GR moves with the attack / release ballistics of the knobs (two-stage release).
    - Colour: the reference is very clean (H2 about -55 dB, H3 about -62 dB at heavy GR); Color 35 % = that,
      more Color adds FET grit. A gentle mid dip and a top-end roll-off reproduce the measured response. */

namespace
{
struct Pt { float x, y; };

float interp (const std::vector<Pt>& t, float x)
{
    if (t.empty()) return 0.0f;
    if (x <= t.front().x)
    {
        if (t.size() < 2) return t.front().y;
        const auto& a = t[0]; const auto& b = t[1];
        return a.y + (b.y - a.y) / (b.x - a.x) * (x - a.x);
    }
    for (size_t i = 1; i < t.size(); ++i)
        if (x <= t[i].x)
        {
            const auto& a = t[i - 1]; const auto& b = t[i];
            return a.y + (b.y - a.y) * (x - a.x) / (b.x - a.x);
        }
    const auto& a = t[t.size() - 2]; const auto& b = t.back();
    return b.y + (b.y - a.y) / (b.x - a.x) * (x - b.x);
}

/** One measured static row: small-signal gain and the output for inputs -40, -36 .. 0 dBFS. */
struct Row { float lin; std::array<float, 11> out; };

/** Gain reduction vs. level after the input stage (L = input + small-signal gain at Output 24). */
std::vector<Pt> buildGr (std::initializer_list<Row> rows)
{
    std::vector<Pt> pts;
    for (auto& r : rows)
        for (int k = 0; k < 11; ++k)
        {
            const float in = -40.0f + 4.0f * (float) k, L = in + r.lin;
            pts.push_back ({ L, jmin (0.0f, r.out[(size_t) k] - L) });
        }
    std::sort (pts.begin(), pts.end(), [] (const Pt& a, const Pt& b) { return a.x < b.x; });
    // merge points closer than 1.5 dB, then make GR non-increasing with level
    std::vector<Pt> m;
    for (auto& p : pts)
    {
        if (! m.empty() && p.x - m.back().x < 1.5f) { m.back().x = 0.5f * (m.back().x + p.x); m.back().y = 0.5f * (m.back().y + p.y); }
        else m.push_back (p);
    }
    for (size_t i = 1; i < m.size(); ++i) m[i].y = jmin (m[i].y, m[i - 1].y);
    // well below threshold there is no gain reduction: pin the start of the table
    m.insert (m.begin(), { m.front().x - 40.0f, 0.0f });
    return m;
}

struct Model
{
    std::vector<Pt> inLaw;                // dial attenuation -> small-signal gain (dB) with Output at 24
    std::vector<Pt> outLaw;               // dial attenuation -> gain change vs. Output 24
    std::array<std::vector<Pt>, 5> gr;    // ratio 4, 8, 12, 20, All
    float allBoost;                       // "all buttons" raises the small-signal gain a little
    float attackMs[3];                    // attack time constant at knob 1, 4, 7 (measured)
    float releaseMs[3];                   // release time constant at knob 1, 4, 7 (fast stage, measured)
    float slowShare;                      // part of the GR that recovers on the slow stage
    float slowMs;                         // slow stage time constant (never shorter than the fast stage)
};

const Model& modelData (int m)
{
    static const Model blue = []
    {
        Model d;
        d.inLaw = { { 0, 26.1f }, { 6, 24.9f }, { 12, 20.79f }, { 18, 14.88f }, { 24, 7.98f }, { 30, 1.78f }, { 36, -5.61f }, { 42, -9.61f }, { 48, -14.18f } };
        d.outLaw = { { 0, 19.80f }, { 6, 18.90f }, { 12, 14.50f }, { 18, 7.40f }, { 24, 0.0f }, { 30, -9.70f }, { 36, -24.35f }, { 48, -38.62f } };
        d.gr[0] = buildGr ({ { 7.98f,  { -32.0f, -28.0f, -24.2f, -22.3f, -21.5f, -20.8f, -20.1f, -19.3f, -18.4f, -17.2f, -15.9f } },
                             { -5.61f, { -45.6f, -41.6f, -37.6f, -33.6f, -29.6f, -25.6f, -22.8f, -21.8f, -21.0f, -20.4f, -19.7f } },
                             { -14.18f,{ -54.2f, -50.2f, -46.2f, -42.2f, -38.2f, -34.2f, -30.2f, -26.2f, -23.0f, -21.9f, -21.1f } } });
        d.gr[1] = buildGr ({ { -5.61f, { -45.6f, -41.6f, -37.6f, -33.6f, -29.6f, -25.6f, -21.6f, -19.9f, -19.5f, -19.1f, -18.7f } },
                             { 7.98f,  { -32.0f, -28.0f, -24.0f, -20.4f, -19.7f, -19.3f, -19.0f, -18.5f, -18.0f, -17.2f, -16.3f } } });
        d.gr[2] = buildGr ({ { -5.61f, { -45.6f, -41.6f, -37.6f, -33.6f, -29.6f, -25.6f, -21.6f, -18.7f, -18.4f, -18.2f, -18.0f } },
                             { 7.98f,  { -32.0f, -28.0f, -24.0f, -20.0f, -18.5f, -18.3f, -18.1f, -17.9f, -17.6f, -17.1f, -16.5f } } });
        d.gr[3] = buildGr ({ { -5.61f, { -45.6f, -41.6f, -37.6f, -33.6f, -29.6f, -25.6f, -21.6f, -17.6f, -17.1f, -17.0f, -16.9f } },
                             { 7.98f,  { -32.0f, -28.0f, -24.0f, -20.0f, -17.2f, -17.1f, -17.0f, -16.9f, -16.7f, -16.5f, -16.2f } } });
        d.gr[4] = buildGr ({ { -5.0f,  { -45.0f, -41.0f, -37.0f, -33.0f, -29.0f, -24.9f, -20.9f, -20.5f, -20.4f, -20.2f, -19.9f } },
                             { 8.6f,   { -31.4f, -27.4f, -23.4f, -20.5f, -20.5f, -20.3f, -20.1f, -19.8f, -19.3f, -18.7f, -17.8f } } });
        d.allBoost = 0.6f;
        d.attackMs[0] = 21.0f; d.attackMs[1] = 11.0f; d.attackMs[2] = 1.9f;
        d.releaseMs[0] = 1650.0f; d.releaseMs[1] = 720.0f; d.releaseMs[2] = 50.0f;
        d.slowShare = 0.15f; d.slowMs = 400.0f;
        return d;
    }();
    static const Model black = []
    {
        Model d;
        d.inLaw = { { 0, 23.57f }, { 6, 23.01f }, { 12, 17.28f }, { 18, 11.84f }, { 24, 4.94f }, { 30, -1.06f }, { 36, -8.65f }, { 42, -12.68f }, { 48, -17.06f } };
        d.outLaw = { { 0, 19.80f }, { 6, 18.90f }, { 12, 14.50f }, { 18, 7.40f }, { 24, 0.0f }, { 30, -9.70f }, { 36, -24.35f }, { 48, -38.62f } };
        d.gr[0] = buildGr ({ { 4.9f,  { -35.1f, -31.1f, -27.0f, -23.0f, -19.8f, -18.8f, -18.1f, -17.6f, -16.9f, -16.1f, -15.1f } },
                             { -8.6f, { -48.6f, -44.6f, -40.6f, -36.6f, -32.6f, -28.6f, -24.6f, -20.8f, -19.1f, -18.4f, -17.8f } },
                             { -17.1f,{ -57.1f, -53.1f, -49.1f, -45.1f, -41.1f, -37.1f, -33.1f, -29.1f, -25.0f, -21.0f, -19.2f } } });
        d.gr[1] = buildGr ({ { -8.6f, { -48.6f, -44.6f, -40.6f, -36.6f, -32.6f, -28.6f, -24.6f, -20.6f, -17.7f, -17.1f, -16.7f } },
                             { 4.9f,  { -35.1f, -31.1f, -27.0f, -23.0f, -19.0f, -17.4f, -17.0f, -16.6f, -16.2f, -15.7f, -15.0f } } });
        d.gr[2] = buildGr ({ { -8.6f, { -48.6f, -44.6f, -40.6f, -36.6f, -32.6f, -28.6f, -24.6f, -20.6f, -16.9f, -16.5f, -16.2f } },
                             { 4.9f,  { -35.1f, -31.1f, -27.0f, -23.0f, -19.0f, -16.6f, -16.4f, -16.1f, -15.9f, -15.6f, -15.2f } } });
        d.gr[3] = buildGr ({ { -8.6f, { -48.6f, -44.6f, -40.6f, -36.6f, -32.6f, -28.6f, -24.6f, -20.6f, -16.6f, -15.3f, -15.1f } },
                             { 4.9f,  { -35.1f, -31.1f, -27.0f, -23.0f, -19.0f, -15.5f, -15.2f, -15.1f, -14.9f, -14.7f, -14.5f } } });
        d.gr[4] = buildGr ({ { -7.4f, { -47.4f, -43.4f, -39.4f, -35.4f, -31.4f, -27.4f, -23.4f, -19.4f, -18.8f, -18.7f, -18.5f } },
                             { 6.1f,  { -33.9f, -29.9f, -25.8f, -21.8f, -18.9f, -18.8f, -18.6f, -18.4f, -18.1f, -17.6f, -17.0f } } });
        d.allBoost = 1.2f;
        d.attackMs[0] = 4.2f; d.attackMs[1] = 2.0f; d.attackMs[2] = 0.33f;   // measured at Input 24 (Black needs more drive to compress)
        d.releaseMs[0] = 2600.0f; d.releaseMs[1] = 1150.0f; d.releaseMs[2] = 215.0f;   // knob 1 does not recover within 2 s: scaled like Blue
        d.slowShare = 0.15f; d.slowMs = 400.0f;
        return d;
    }();
    return m == 1 ? black : blue;
}

float knobTime (float knob, const float* v)   // knob 1..7 through the times measured at 1, 4 and 7 (log interpolation)
{
    const float k = jlimit (1.0f, 7.0f, knob);
    if (k <= 4.0f) return v[0] * std::pow (v[1] / v[0], (k - 1.0f) / 3.0f);
    return v[1] * std::pow (v[2] / v[1], (k - 4.0f) / 3.0f);
}
} // namespace

void FetComp::prepare (double sampleRate)
{
    sr = sampleRate;
    const float fs = (float) sr;
    // measured response re 1 kHz, per model (Blue: +0.45 dB plateau, narrow dip at 1 kHz, -0.2 dB @ 10 kHz, -2.6 dB @ 16 kHz;
    // Black: +0.3 dB gentle rise towards the top, no high roll-off, slightly less at 30 Hz)
    auto peaking = [fs] (float hz, float db, float q)
    {
        const float A = std::pow (10.0f, db / 40.0f), w = 2.0f * MathConstants<float>::pi * hz / fs, al = std::sin (w) / (2.0f * q), c = std::cos (w);
        const float a0 = 1.0f + al / A;
        return std::array<float, 5> { (1.0f + al * A) / a0, -2.0f * c / a0, (1.0f - al * A) / a0, -2.0f * c / a0, (1.0f - al / A) / a0 };
    };
    auto lowpass = [fs] (float hz, float q)
    {
        const float w = 2.0f * MathConstants<float>::pi * jmin (hz, fs * 0.42f) / fs, al = std::sin (w) / (2.0f * q), c = std::cos (w), a0 = 1.0f + al;
        return std::array<float, 5> { (1.0f - c) * 0.5f / a0, (1.0f - c) / a0, (1.0f - c) * 0.5f / a0, -2.0f * c / a0, (1.0f - al) / a0 };
    };
    auto lowShelf = [fs] (float hz, float db)   // RBJ, S = 1
    {
        const float A = std::pow (10.0f, db / 40.0f), w = 2.0f * MathConstants<float>::pi * hz / fs, c = std::cos (w), al = std::sin (w) / 2.0f * std::sqrt (2.0f), sa = 2.0f * std::sqrt (A) * al;
        const float a0 = (A + 1.0f) + (A - 1.0f) * c + sa;
        return std::array<float, 5> { A * ((A + 1.0f) - (A - 1.0f) * c + sa) / a0, 2.0f * A * ((A - 1.0f) - (A + 1.0f) * c) / a0, A * ((A + 1.0f) - (A - 1.0f) * c - sa) / a0,
                                      -2.0f * ((A - 1.0f) + (A + 1.0f) * c) / a0, ((A + 1.0f) + (A - 1.0f) * c - sa) / a0 };
    };
    pkC[0] = peaking (1000.0f, -0.5f, 1.2f);  lpC[0] = lowpass (16000.0f, 0.7071f); hiC[0] = peaking (10000.0f, -0.55f, 1.4f);  lsC[0] = lowShelf (40.0f, -0.35f); makeupDb[0] = 0.45f;
    pkC[1] = peaking (1000.0f, -0.3f, 1.2f);  lpC[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; hiC[1] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }; lsC[1] = lowShelf (40.0f, -0.45f); makeupDb[1] = 0.3f;
    lfA = std::exp (-2.0f * MathConstants<float>::pi * 300.0f / fs);
    evA = std::exp (-2.0f * MathConstants<float>::pi * 150.0f / fs);
    reset();
}

void FetComp::reset()
{
    for (int c = 0; c < 2; ++c) { hpZ[c] = 0.0f; grFast[c] = grSlow[c] = 0.0f; pkHold[c] = 0.0f; pkCnt[c] = 0; for (auto& z : pkZ[c]) z = 0.0f; for (auto& z : lpZ[c]) z = 0.0f; for (auto& z : lsZ[c]) z = 0.0f; lfZ[c] = evZ[c] = 0.0f; for (auto& z : hiZ[c]) z = 0.0f; }
    grSm = 0.0f;
}

float FetComp::process (float* l, float* r, int n, int channels)
{
    const int mi = model == 1 ? 1 : 0;
    const auto& M = modelData (mi);
    const auto& pk = pkC[(size_t) mi];
    const auto& lp = lpC[(size_t) mi];
    const auto& ls = lsC[(size_t) mi];
    const auto& hi = hiC[(size_t) mi];
    const int ri = jlimit (0, 4, ratio);
    const auto& grTab = M.gr[(size_t) ri];
    const float fs = (float) sr;

    // dial positions behind the dB values the hosts use (input "0 dB" = dial 48, output "0 dB" = dial 12)
    const float attIn = 48.0f - inputDb, attOut = 12.0f - outputDb;
    float gInDb = interp (M.inLaw, jmin (48.0f, attIn));
    if (attIn > 48.0f) gInDb -= (attIn - 48.0f) * 1.6f;   // towards "infinity" (fully counter-clockwise)
    if (ri == 4) gInDb += M.allBoost;
    const float gOutDb = interp (M.outLaw, jlimit (0.0f, 48.0f, attOut)) - (attOut > 48.0f ? (attOut - 48.0f) * 1.6f : 0.0f);
    // +0.45 dB: the response plateau (the knob laws were measured at 1 kHz, which sits in the dip)
    const float gin = Decibels::decibelsToGain (gInDb), gout = Decibels::decibelsToGain (gOutDb + makeupDb[mi]);

    const float attMs = knobTime (attackKnob, M.attackMs);
    const float relMs = knobTime (releaseKnob, M.releaseMs);
    const float aAtt = std::exp (-1.0f / (attMs * 0.001f * fs));
    const float aRelF = std::exp (-1.0f / (relMs * 0.001f * fs));
    const float aRelS = std::exp (-1.0f / (jmax (relMs, M.slowMs) * 0.001f * fs));
    // peak hold (12 ms covers a 100 Hz cycle) so low notes do not ripple the gain and add distortion
    const int holdN = (int) (0.012f * fs);
    const float aPkFall = std::exp (-1.0f / (0.004f * fs));
    const float hpA = scHpf > 0.0f ? std::exp (-2.0f * MathConstants<float>::pi * scHpf / fs) : 0.0f;
    // harmonics (Color 35 % = the reference): H2 about -55..-59 dB; H3 -66 dB at 1 kHz with light GR, -62 dB with heavy GR,
    // and about -61 dB on low notes (extra low-frequency odd term that fades as the gain reduction grows)
    // Black: H2 rises with frequency (even term on a 300 Hz high-passed copy), lower H3, no extra low-note term
    const float cm = color / 0.35f, cAmt = cm * cm * (ri == 4 ? 1.8f : 1.0f);
    const bool blk = mi == 1;
    const float a2 = (blk ? 0.053f : 0.042f) * cAmt, a3 = (blk ? 0.245f : 0.34f) * cAmt, a3lf = blk ? 0.0f : 0.55f * cAmt, lightK = blk ? 1.6f : 0.65f;

    float grMin = 0.0f, ipk = 0.0f, opk = 0.0f;
    float* ch[2] = { l, channels > 1 ? r : l };
    for (int i = 0; i < n; ++i)
    {
        float x[2] = { ch[0][i] * gin, ch[1][i] * gin };
        float target[2];
        for (int c = 0; c < 2; ++c)
        {
            float s = keyL != nullptr ? (c ? keyR : keyL)[i] * gin : x[c];
            if (hpA > 0.0f) { hpZ[c] = s + hpA * (hpZ[c] - s); s -= hpZ[c]; }
            const float a = std::abs (s);
            if (a >= pkHold[c]) { pkHold[c] = a; pkCnt[c] = holdN; }
            else if (pkCnt[c] > 0) --pkCnt[c];
            else pkHold[c] = a + aPkFall * (pkHold[c] - a);
            const float lv = 20.0f * std::log10 (jmax (1e-6f, pkHold[c]));
            target[c] = jmin (0.0f, interp (grTab, lv));
        }
        if (link) target[0] = target[1] = jmin (target[0], target[1]);

        float gr[2];
        for (int c = 0; c < 2; ++c)
        {
            // attack pulls the fast GR down; release: a fast stage and a slow (program dependent) stage
            const float t = target[c];
            float& f = grFast[c];
            float& sl = grSlow[c];
            const float tot = f + sl;
            if (t < tot) { const float d = (t - tot) * (1.0f - aAtt); f += d * (1.0f - M.slowShare); sl += d * M.slowShare; }
            else { f = t * (1.0f - M.slowShare) + aRelF * (f - t * (1.0f - M.slowShare)); sl = t * M.slowShare + aRelS * (sl - t * M.slowShare); }
            gr[c] = f + sl;
        }

        for (int c = 0; c < 2; ++c)
        {
            grMin = jmin (grMin, gr[c]);
            float y = x[c] * Decibels::decibelsToGain (gr[c]);
            if (cAmt > 0.0001f)
            {
                lfZ[c] = y + lfA * (lfZ[c] - y);
                const float lf = lfZ[c], light = 1.0f - jmin (1.0f, -gr[c] / 6.0f);
                const float a3now = a3 * (1.0f + lightK * light), ev = blk ? y - (evZ[c] = y + evA * (evZ[c] - y)) : y;
                y = y + a2 * ev * ev + a3now * y * y * y + a3lf * light * lf * lf * lf;   // FET even + odd harmonics
            }
            // response colour
            {
                auto& z = pkZ[c];
                const float o = pk[0] * y + z[0];
                z[0] = pk[1] * y - pk[3] * o + z[1];
                z[1] = pk[2] * y - pk[4] * o;
                y = o;
            }
            {
                auto& z = lsZ[c];
                const float o = ls[0] * y + z[0];
                z[0] = ls[1] * y - ls[3] * o + z[1];
                z[1] = ls[2] * y - ls[4] * o;
                y = o;
            }
            {
                auto& z = hiZ[c];
                const float o = hi[0] * y + z[0];
                z[0] = hi[1] * y - hi[3] * o + z[1];
                z[1] = hi[2] * y - hi[4] * o;
                y = o;
            }
            {
                auto& z = lpZ[c];
                const float o = lp[0] * y + z[0];
                z[0] = lp[1] * y - lp[3] * o + z[1];
                z[1] = lp[2] * y - lp[4] * o;
                y = o;
            }
            y *= gout;
            ipk = jmax (ipk, std::abs (ch[c][i]));
            const float out = ch[c][i] * (1.0f - mix) + y * mix;
            opk = jmax (opk, std::abs (out));
            x[c] = out;
        }
        ch[0][i] = x[0];
        if (channels > 1) ch[1][i] = x[1];
    }
    inPeak = ipk;
    outPeak = opk;
    return grMin;
}
