#include "EchoEngine.h"
#include <algorithm>

namespace si::echo
{
namespace
{
constexpr float kPi = 3.14159265358979f;
inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }

struct Style
{
    const char* name;
    float wow, wowRate, flutter, chorus;   // modulation
    float sat; int satType;                // 0 tape, 1 tube, 2 hard
    float lp, hp;                          // loop band limits
    int bits;                              // 16 = off
    float noise, diff;
};
const Style kStyles[kNumStyles] = {
    { "Clean Digital", 0.00f, 0.0f, 0.00f, 0.00f, 0.00f, 0, 20000, 20,   16, 0.000f, 0.0f },
    { "Studio Tape",   0.05f, 0.6f, 0.02f, 0.00f, 0.30f, 0, 12000, 60,   16, 0.002f, 0.0f },
    { "Master Tape",   0.06f, 0.6f, 0.03f, 0.00f, 0.20f, 0, 15000, 40,   16, 0.001f, 0.0f },
    { "Cheap Tape",    0.45f, 1.1f, 0.25f, 0.00f, 0.50f, 0, 6000,  150,  16, 0.010f, 0.0f },
    { "Space Echo",    0.32f, 0.6f, 0.15f, 0.00f, 0.60f, 0, 5000,  120,  16, 0.006f, 0.1f },
    { "Memory Man",    0.05f, 0.5f, 0.00f, 0.45f, 0.30f, 1, 4200,  80,   16, 0.004f, 0.0f },
    { "DM-2",          0.04f, 0.5f, 0.00f, 0.25f, 0.40f, 1, 2800,  90,   16, 0.004f, 0.0f },
    { "Analog Warm",   0.03f, 0.4f, 0.00f, 0.10f, 0.40f, 1, 7000,  60,   16, 0.002f, 0.0f },
    { "Lo-Fi 12 Bit",  0.00f, 0.0f, 0.00f, 0.00f, 0.20f, 2, 9000,  100,  12, 0.000f, 0.0f },
    { "Telephone",     0.00f, 0.0f, 0.02f, 0.00f, 0.50f, 2, 3000,  500,  16, 0.003f, 0.0f },
    { "AM Radio",      0.05f, 0.3f, 0.02f, 0.00f, 0.30f, 1, 4500,  300,  16, 0.015f, 0.0f },
    { "Transmitter",   0.10f, 2.0f, 0.10f, 0.00f, 0.40f, 2, 3500,  400,  10, 0.020f, 0.0f },
    { "Ambient",       0.05f, 0.3f, 0.00f, 0.20f, 0.10f, 1, 8000,  100,  16, 0.000f, 0.7f },
    { "Diffuse Tape",  0.15f, 0.7f, 0.05f, 0.00f, 0.30f, 0, 9000,  80,   16, 0.002f, 0.5f },
    { "Bright Digital",0.00f, 0.0f, 0.00f, 0.00f, 0.05f, 2, 20000, 200,  16, 0.000f, 0.0f },
    { "Dub Echo",      0.25f, 0.5f, 0.10f, 0.00f, 0.70f, 0, 3500,  200,  16, 0.004f, 0.1f },
};
const char* kPatternNames[kNumPatterns] = { "Straight", "Gallop", "Triplet Run", "Swing", "Ramp Up", "Ramp Down", "Stutter", "Sparse" };
const std::array<float, 8> kPatterns[kNumPatterns] = {
    { 1.0f, 0.8f, 0.65f, 0.5f, 0.4f, 0.3f, 0.22f, 0.15f },
    { 1.0f, 0.0f, 0.7f, 0.6f, 0.0f, 0.45f, 0.35f, 0.0f },
    { 1.0f, 0.75f, 0.5f, 0.0f, 0.7f, 0.5f, 0.3f, 0.0f },
    { 1.0f, 0.0f, 0.6f, 0.0f, 0.5f, 0.0f, 0.35f, 0.0f },
    { 0.15f, 0.25f, 0.35f, 0.5f, 0.65f, 0.8f, 0.9f, 1.0f },
    { 1.0f, 0.85f, 0.7f, 0.55f, 0.4f, 0.28f, 0.18f, 0.1f },
    { 1.0f, 0.9f, 0.9f, 0.0f, 0.0f, 0.6f, 0.6f, 0.6f },
    { 1.0f, 0.0f, 0.0f, 0.55f, 0.0f, 0.0f, 0.3f, 0.0f },
};
} // namespace

const char* styleName (int s) { return kStyles[std::clamp (s, 0, kNumStyles - 1)].name; }
const char* patternName (int p) { return kPatternNames[std::clamp (p, 0, kNumPatterns - 1)]; }
const std::array<float, 8>& pattern (int p) { return kPatterns[std::clamp (p, 0, kNumPatterns - 1)]; }

float EchoEngine::Line::read (double d) const
{
    const int N = (int) buf.size();
    const double pos = (double) w - 1.0 - std::max (1.0, d);
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return buf[(size_t) (((k % N) + N) % N)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

float EchoEngine::Ap::process (float x, int d, float g)
{
    const int N = (int) b.size();
    const float v = b[(size_t) ((w - d + N) % N)];
    const float in = x + g * v;
    b[(size_t) w] = in;
    w = (w + 1) % N;
    return v - g * in;
}

void EchoEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& l : line) l.buf.assign ((size_t) (sr * 8.5) + 8, 0.0f);
    for (auto& c : ap) for (auto& a : c) a.b.assign ((size_t) (sr * 0.03) + 4, 0.0f);
    reset();
}

void EchoEngine::reset()
{
    for (auto& l : line) { std::fill (l.buf.begin(), l.buf.end(), 0.0f); l.w = 0; }
    for (auto& c : ap) for (auto& a : c) { std::fill (a.b.begin(), a.b.end(), 0.0f); a.w = 0; }
    for (int c = 0; c < 2; ++c) hp1[c] = hp2[c] = lp1[c] = lp2[c] = lq1[c] = lq2[c] = held[c] = 0.0f;
    smT[0] = prm.timeL; smT[1] = prm.timeR;
    duckEnv = envIn = wetEnv = 0.0f;
}

float EchoEngine::loop (int c, float x)
{
    const auto& st = kStyles[styleNow()];
    const float fs = (float) sr;
    // diffusion smear
    const float diff = clampf (prm.diffusion + st.diff, 0.0f, 1.0f);
    if (diff > 0.001f)
    {
        const float ms[3] = { 3.1f, 5.3f, 8.7f };
        for (int k = 0; k < 3; ++k) x = ap[(size_t) c][(size_t) k].process (x, (int) (ms[k] * 0.001f * fs * (c ? 1.13f : 1.0f)), 0.62f * diff);
    }
    // band limits shaped like H-Delay's (measured against a recording): HiPass 12 dB/oct,
    // LoPass 12 dB/oct + a second 12 dB/oct stage an octave above, so the top falls away steeply
    {
        const float v3 = x - hp2[c], v1 = hpA1 * hp1[c] + hpA2 * v3, v2 = hp2[c] + hpA2 * hp1[c] + hpA3 * v3;
        hp1[c] = 2.0f * v1 - hp1[c]; hp2[c] = 2.0f * v2 - hp2[c];
        x = x - kHpK * v1 - v2;
    }
    {
        const float v3 = x - lp2[c], v1 = lpA1 * lp1[c] + lpA2 * v3, v2 = lp2[c] + lpA2 * lp1[c] + lpA3 * v3;
        lp1[c] = 2.0f * v1 - lp1[c]; lp2[c] = 2.0f * v2 - lp2[c];
        x = v2;
    }
    {
        const float v3 = x - lq2[c], v1 = lqA1 * lq1[c] + lqA2 * v3, v2 = lq2[c] + lqA2 * lq1[c] + lqA3 * v3;
        lq1[c] = 2.0f * v1 - lq1[c]; lq2[c] = 2.0f * v2 - lq2[c];
        x = v2;
    }
    // saturation with headroom: clean up to ~0 dBFS at Saturation 0, the style only adds a little colour,
    // the Saturation knob drives up to +18 dB into the curve (unity small-signal gain in every case)
    const float amount = clampf (0.4f * st.sat + prm.drive, 0.0f, 1.5f);
    const float k = 0.5f * std::pow (10.0f, amount * 0.9f);
    switch (st.satType)
    {
        case 0: x = std::atan (k * x) / k; break;
        case 1: { const float b = 0.12f; x = (std::tanh (k * x + b) - std::tanh (b)) / (k * (1.0f - std::tanh (b) * std::tanh (b))); break; }
        default: x = clampf (k * x, -1.0f, 1.0f) / k; break;
    }
    // bit / rate reduction
    const int bits = prm.lofi ? std::min (st.bits, 10) : st.bits;
    if (bits < 16)
    {
        if (c == 0) ++holdCount;
        if (holdCount % std::max (1, (int) (sr / 16000.0)) == 0) held[c] = x;
        const float q = std::pow (2.0f, 1.0f - (float) bits);
        x = std::round (held[c] / q) * q;
    }
    // safety: never runs away even with feedback > 100 %
    return std::tanh (x * 0.4f) / 0.4f;
}

void EchoEngine::updateFilters()
{
    const auto& st = kStyles[styleNow()];
    const float fs = (float) sr, nyq = 0.45f * fs;
    const float hpHz = std::min (nyq, std::max (prm.lowCut, st.hp));
    const float lpHz = std::min ({ nyq, prm.highCut, st.lp, prm.lofi ? 4000.0f : 20000.0f });
    auto coeffs = [fs] (float hz, float k, float& a1, float& a2, float& a3)
    {
        const float g = std::tan (kPi * hz / fs);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    };
    coeffs (hpHz, kHpK, hpA1, hpA2, hpA3);
    coeffs (lpHz, 1.41421356f, lpA1, lpA2, lpA3);
    coeffs (std::min (nyq, lpHz * 2.2f), 1.41421356f, lqA1, lqA2, lqA3);
}

void EchoEngine::process (float* l, float* r, int n)
{
    updateFilters();
    const auto& st = kStyles[styleNow()];
    const float fs = (float) sr;
    const float fb = prm.freeze ? 1.0f : clampf (prm.feedback, 0.0f, 1.1f);
    const float glide = 1.0f - std::exp (-1.0f / (0.06f * fs));
    const float wowD = (st.wow + prm.wobble * 0.6f) * 0.0025f * fs;
    const float flutD = (st.flutter + prm.wobble * 0.2f) * 0.0004f * fs;
    const float chorD = st.chorus * 0.0018f * fs;
    const double wowInc = (st.wowRate > 0.0f ? st.wowRate : 0.6f) / sr, flutInc = 7.0 / sr, chorInc = 0.9 / sr;
    const float duckA = std::exp (-1.0f / (0.005f * fs)), duckR = std::exp (-1.0f / (0.3f * fs));
    const float envR = std::exp (-1.0f / (0.1f * fs));
    const double maxD = (double) line[0].buf.size() - 8.0;
    const auto& pat = pattern (prm.patternIdx);
    const float outG = std::pow (10.0f, prm.outputDb * 0.05f);

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i], inR = r[i];
        const float ax = keyL != nullptr ? std::max (std::abs (keyL[i]), std::abs (keyR[i])) : std::max (std::abs (inL), std::abs (inR));
        duckEnv = ax > duckEnv ? ax + duckA * (duckEnv - ax) : ax + duckR * (duckEnv - ax);
        envIn = std::max (ax, envIn * envR);

        // tape-like glide when the time changes
        smT[0] += (prm.timeL - smT[0]) * glide;
        smT[1] += (prm.timeR - smT[1]) * glide;
        wowPh += wowInc; if (wowPh >= 1.0) wowPh -= 1.0;
        flutPh += flutInc; if (flutPh >= 1.0) flutPh -= 1.0;
        chorPh += chorInc; if (chorPh >= 1.0) chorPh -= 1.0;
        modPh += prm.modRate / sr; if (modPh >= 1.0) modPh -= 1.0;
        if (i % 512 == 0) driftT = rnd();
        drift += (driftT - drift) * 0.0002f;
        const float mod = wowD * ((float) std::sin (wowPh * 6.2831853) + 0.4f * drift) + flutD * (float) std::sin (flutPh * 6.2831853)
                        + prm.modDepth * 0.003f * fs * (float) std::sin (modPh * 6.2831853);
        const float ch = chorD * (float) std::sin (chorPh * 6.2831853);

        float wl = 0.0f, wr = 0.0f;
        const float inMono = prm.freeze ? 0.0f : 0.5f * (inL + inR);
        switch (prm.mode)
        {
            case Mode::Single:
            case Mode::Dual:
            {
                const double tR = prm.mode == Mode::Single ? smT[0] : smT[1];
                const double dL = std::min (maxD, smT[0] * sr + mod + ch);
                const double dR = std::min (maxD, tR * sr * (1.0 + 0.5 * prm.groove * 0.25) + mod - ch);
                wl = line[0].read (dL);
                wr = line[1].read (dR);
                // input + feedback both go through the filters / saturation, so the first echo is already shaped
                line[0].write (prm.freeze ? wl : loop (0, inL + fb * wl));
                line[1].write (prm.freeze ? wr : loop (1, inR + fb * wr));
                break;
            }
            case Mode::PingPong:
            {
                // Waves H-Delay topology: input -> left line -> right line at full level (the right side repeats the
                // left one a beat later, through the filters), and only the right side feeds back into the left.
                // Left and right stay hard-panned; Width narrows (or widens) them in the M/S stage below.
                const double dL = std::min (maxD, smT[0] * sr + mod + ch);
                const double dR = std::min (maxD, smT[0] * sr * (1.0 + 0.5 * prm.groove) + mod - ch);
                wl = line[0].read (dL);
                wr = line[1].read (dR);
                line[0].write (prm.freeze ? wr : loop (0, inMono + fb * wr));
                line[1].write (prm.freeze ? wl : loop (1, wl));
                break;
            }
            case Mode::Rhythm:
            {
                const double step = smT[0] * sr;
                float last = 0.0f;
                for (int k = 0; k < 8; ++k)
                {
                    const double d = std::min (maxD, step * (k + 1) * (k % 2 ? 1.0 + 0.33 * prm.groove : 1.0) + mod + ch);
                    const float v = line[0].read (d) * pat[(size_t) k];
                    if (k % 2) wr += v; else wl += v;
                    if (k == 7) last = line[0].read (d);
                }
                const float pan = 0.35f;
                const float ml = wl, mr = wr;
                wl = ml + pan * mr;
                wr = mr + pan * ml;
                line[0].write (prm.freeze ? last : loop (0, inMono + fb * 0.6f * last));
                break;
            }
        }

        // width + hiss + ducking
        {
            const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * prm.width;
            wl = mid + side;
            wr = mid - side;
        }
        if (st.noise > 0.0f) { wl += st.noise * 0.05f * rnd(); wr += st.noise * 0.05f * rnd(); }
        const float duck = 1.0f - prm.ducking * clampf (duckEnv * 4.0f, 0.0f, 1.0f);
        wl *= duck;
        wr *= duck;
        wetEnv = std::max (std::max (std::abs (wl), std::abs (wr)), wetEnv * envR);

        const float dryG = std::cos (prm.mix * 1.5707963f), wetG = std::sin (prm.mix * 1.5707963f);
        if (prm.phaseL) wl = -wl;
        if (prm.phaseR) wr = -wr;
        l[i] = (inL * dryG + wl * wetG) * outG;
        r[i] = (inR * dryG + wr * wetG) * outG;
    }
    wetLevel = wetEnv;
    inLevel = envIn;
}
} // namespace si::echo

namespace si::echo
{
int EchoEngine::styleNow() const
{
    // H-Delay "Analog" models override the character: 1 warm BBD, 2 chorused BBD, 3 dark BBD, 4 tape echo
    static const int analogStyle[5] = { -1, 7, 5, 6, 4 };
    if (prm.analog > 0 && prm.analog <= 4) return analogStyle[prm.analog];
    return std::clamp (prm.style, 0, kNumStyles - 1);
}
} // namespace si::echo
