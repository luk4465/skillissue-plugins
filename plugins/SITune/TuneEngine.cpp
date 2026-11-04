#include "TuneEngine.h"
#include <algorithm>

namespace si::tune
{
namespace
{
inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }

inline float hermite (const std::vector<float>& b, int mask, double pos)
{
    const int i = (int) std::floor (pos);
    const float f = (float) (pos - i);
    const float xm1 = b[(size_t) ((i - 1) & mask)], x0 = b[(size_t) (i & mask)], x1 = b[(size_t) ((i + 1) & mask)], x2 = b[(size_t) ((i + 2) & mask)];
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
} // namespace

void TuneEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    bufL.assign (kBuf, 0.0f);
    bufR.assign (kBuf, 0.0f);
    reset();
}

void TuneEngine::reset()
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    dec.fill (0.0f);
    w = decW = decCount = sinceHop = 0;
    lp1 = lp2 = 0.0f;
    period = 0.0f;
    midiNow = midiSlow = midiPrev = -1.0f;
    velSm = holdTime = 0.0f;
    heldNote = candNote = -1;
    candTime = 0.0f;
    shift = shiftTarget = prm.transpose;
    rmsEnv = 0.0f;
    D = D2 = prm.lowLatency ? 4.0 : 0.006 * sr;
    fade = 1.0f;
}

int TuneEngine::latencySamples() const
{
    // Zero mode reads straight behind the write head (no reported latency, like hardware tuners);
    // Quality mode keeps a 6 ms cushion for longer, smoother splices and reports it.
    return prm.lowLatency ? 0 : (int) std::round (0.0075 * sr);
}

float TuneEngine::nearestAllowed (float m) const
{
    const int mask = prm.scaleMask & 0xFFF;
    const int r = (int) std::round (m);
    if (mask == 0) return (float) r;
    int best = r;
    float bestD = 1e9f;
    for (int c = r - 7; c <= r + 7; ++c)
    {
        const int pc = ((c % 12) + 12) % 12;
        if (! (mask & (1 << pc))) continue;
        const float d = std::abs ((float) c - m);
        if (d < bestD) { bestD = d; best = c; }
    }
    return (float) best;
}

void TuneEngine::detect()
{
    const double srd = sr / kDec;
    constexpr int W = 256;
    static const double kRange[5][2] = { { 150.0, 1300.0 }, { 75.0, 1100.0 }, { 55.0, 520.0 }, { 40.0, 2000.0 }, { 25.0, 420.0 } };
    const auto& rg = kRange[std::min (4, std::max (0, prm.inputType))];
    const int tauMin = std::max (2, (int) (srd / rg[1]));
    const int tauMax = std::min (kDecBuf - W - 2, (int) (srd / rg[0]));
    const float yinThr = 0.06f + 0.25f * clampf (prm.tracking, 0.0f, 1.0f);
    float x[kDecBuf];
    const int len = W + tauMax;
    for (int i = 0; i < len; ++i) x[i] = dec[(size_t) ((decW - len + i + kDecBuf) % kDecBuf)];

    // YIN difference + cumulative mean normalisation
    float d[kDecBuf];
    d[0] = 1.0f;
    float run = 0.0f;
    int best = -1;
    float bestVal = 1e9f;
    for (int t = 1; t <= tauMax; ++t)
    {
        float acc = 0.0f;
        for (int j = 0; j < W; ++j)
        {
            const float diff = x[j] - x[j + t];
            acc += diff * diff;
        }
        run += acc;
        d[t] = run > 0.0f ? acc * (float) t / run : 1.0f;
    }
    for (int t = tauMin; t < tauMax; ++t)
    {
        if (d[t] < yinThr)
        {
            while (t + 1 < tauMax && d[t + 1] < d[t]) ++t;
            best = t;
            bestVal = d[t];
            break;
        }
        if (d[t] < bestVal) { bestVal = d[t]; best = t; }
    }

    const bool voiced = best > 0 && bestVal < yinThr * 2.2f && rmsEnv > 0.003f;
    confidence = voiced ? clampf (1.0f - bestVal, 0.0f, 1.0f) : 0.0f;
    if (! voiced)
    {
        period = 0.0f;
        midiNow = -1.0f;
        midiSlow = midiPrev = -1.0f;
        velSm = holdTime = 0.0f;
        shiftTarget = prm.transpose;
        inMidi = -1.0f;
        targetMidi = -1.0f;
    }
    else
    {
        // parabolic interpolation on the decimated curve
        float tau = (float) best;
        if (best > 1 && best < tauMax - 1)
        {
            const float a = d[best - 1], b = d[best], c = d[best + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1e-9f) tau += 0.5f * (a - c) / den;
        }
        // refine at full rate around 4*tau
        const int mask = kBuf - 1;
        constexpr int Wf = 512;
        const int c0 = (int) std::round (tau * kDec);
        float dl[9];
        for (int k = -4; k <= 4; ++k)
        {
            const int lag = std::max (1, c0 + k);
            float acc = 0.0f;
            for (int j = 0; j < Wf; ++j)
            {
                const int i0 = (w - 1 - Wf - lag + j) & mask;
                const int i1 = (w - 1 - Wf + j) & mask;
                const float a0 = bufL[(size_t) i0] + bufR[(size_t) i0], a1 = bufL[(size_t) i1] + bufR[(size_t) i1];
                const float diff = a0 - a1;
                acc += diff * diff;
            }
            dl[k + 4] = acc;
        }
        int mi = 4;
        for (int k = 1; k < 8; ++k) if (dl[k] < dl[mi]) mi = k;
        float ref = (float) (c0 + mi - 4);
        if (mi > 0 && mi < 8)
        {
            const float a = dl[mi - 1], b = dl[mi], c = dl[mi + 1];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1e-12f) ref += 0.5f * (a - c) / den;
        }
        period = clampf (ref, (float) sr / 1200.0f, (float) sr / 60.0f);
        midiNow = 69.0f + 12.0f * std::log2 ((float) (sr / period) / prm.refHz);
        const float hop = (float) (64 * kDec / sr);
        // light smoothing of the detected pitch: detection noise must not turn into audible "grain"
        if (midiPrev > 0.0f && std::abs (midiNow - midiPrev) < 0.5f) midiNow = midiPrev + 0.6f * (midiNow - midiPrev);
        const float vel = midiPrev > 0.0f ? (midiNow - midiPrev) / hop : 0.0f;   // semitones per second
        velSm += 0.4f * (vel - velSm);
        midiPrev = midiNow;
        inMidi = midiNow;

        // note choice (optional stabiliser = hysteresis against flutter)
        static const float stab[] = { 0.0f, 0.04f, 0.09f, 0.18f };
        const int cand = (int) nearestAllowed (midiNow);
        const int before = heldNote;
        if (heldNote < 0 || std::abs (midiNow - (float) heldNote) > 1.2f) heldNote = cand;
        else if (cand != heldNote)
        {
            if (cand == candNote) candTime += hop;
            else { candNote = cand; candTime = hop; }
            if (candTime >= stab[std::min (3, std::max (0, prm.stabilizer))]) heldNote = cand;
        }
        holdTime = heldNote == before ? holdTime + hop : 0.0f;
        targetMidi = (float) heldNote;

        // predict where the pitch is "now": detection looks ~12 ms into the past, vibrato would slip through
        const float midiPred = midiNow + clampf (velSm * 0.012f, -0.3f, 0.3f);
        float dev = (float) heldNote - midiPred;

        // Humanize (like Auto-Tune): sustained notes are retuned more slowly than short ones, so long notes keep life
        speedEff = prm.speedMs * (1.0f + 20.0f * prm.humanize * clampf (holdTime / 0.25f, 0.0f, 1.0f));

        // Flex Tune (like Auto-Tune): while the pitch is moving (scoops, slides, vibrato) correction backs off,
        // and notes far from the target are left more natural; steady notes near the target are fully tuned
        if (prm.flex > 0.0f)
        {
            const float f = clampf (prm.flex, 0.0f, 1.0f);
            const float v0 = 2.0f + 20.0f * (1.0f - f) * (1.0f - f) * (1.0f - f);
            const float moving = std::abs (velSm) / v0;
            dev *= 1.0f / (1.0f + moving * moving);
            const float ad = std::abs ((float) heldNote - midiNow);
            const float t = clampf ((ad - 0.1f) / 0.9f, 0.0f, 1.0f);
            dev *= 1.0f - f * t * t * (3.0f - 2.0f * t);
        }
        // Natural Vibrato: scale the singer's own vibrato (deviation from the slow pitch average)
        midiSlow = midiSlow < 0.0f ? midiNow : midiSlow + 0.035f * (midiNow - midiSlow);
        const float vib = (midiNow - midiSlow) * (std::pow (10.0f, prm.vibratoDb / 20.0f) - 1.0f);
        shiftTarget = clampf (dev * prm.amount + vib + prm.transpose, -24.0f, 24.0f);
    }

    const int hp = histPos.load();
    histIn[(size_t) hp].store (midiNow);
    histOut[(size_t) hp].store (midiNow < 0.0f ? -1.0f : midiNow + shift);
    histPos.store ((hp + 1) % kHist);
}

void TuneEngine::process (float* l, float* r, int n)
{
    const int mask = kBuf - 1;
    const float envRel = std::exp (-1.0f / (0.05f * (float) sr));
    const float lpA = std::exp (-2.0f * 3.14159265f * 2400.0f / (float) sr);
    // Retune Speed: tau = 0.6 * speed (~80 % of the way after "speed" ms); Humanize stretches it on held notes
    const float sp = prm.humanize > 0.0f ? std::max (prm.speedMs, speedEff) : prm.speedMs;
    const float speedA = sp <= 0.5f ? 0.0f : std::exp (-1.0f / (sp * 0.6f * 0.001f * (float) sr));
    const double dMin = prm.lowLatency ? 3.0 : 0.006 * sr;   // Zero mode: read right behind the write head
    const double xMax = (prm.lowLatency ? 0.0015 : 0.004) * sr;

    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i], inR = r[i];
        bufL[(size_t) w] = inL;
        bufR[(size_t) w] = inR;
        w = (w + 1) & mask;
        const float x = 0.5f * (inL + inR);
        const float ax = std::abs (x);
        rmsEnv = ax > rmsEnv ? ax : ax + envRel * (rmsEnv - ax);

        // decimate for detection
        lp1 = x + lpA * (lp1 - x);
        lp2 = lp1 + lpA * (lp2 - lp1);
        if (++decCount % kDec == 0)
        {
            dec[(size_t) decW] = lp2;
            decW = (decW + 1) % kDecBuf;
            if (++sinceHop >= 64 && decCount > kDec * 700)
            {
                sinceHop = 0;
                detect();
            }
        }

        // retune speed
        shift = shiftTarget + speedA * (shift - shiftTarget);
        ratio = std::pow (2.0f, shift / 12.0f);

        // pitch-synchronous delay-line shifter
        const double P = period > 0.0f ? (double) period : 0.006 * sr;
        D += 1.0 - ratio;
        D2 += 1.0 - ratio;
        if (fade >= 1.0f)
        {
            const double X = std::min (P * (prm.lowLatency ? 0.5 : 1.0), xMax);
            // jump early enough that the fading-out tap never runs into the write head
            const double dLow = dMin + std::max (0.0, (double) ratio - 1.0) * X + 1.0;
            if (D < dLow) { D2 = D; D += P; fade = 0.0f; fadeInc = (float) (1.0 / std::max (8.0, X)); }
            else if (D > dLow + P + X) { D2 = D; D -= P; fade = 0.0f; fadeInc = (float) (1.0 / std::max (8.0, X)); }
            else if (prm.lowLatency && rmsEnv < 0.0005f && D > dLow + 16.0)
            {
                // re-centre onto the write head during silence so the next phrase starts with no delay
                D2 = D; D = dLow; fade = 0.0f; fadeInc = 1.0f / 128.0f;
            }
        }
        D = std::max (3.0, std::min ((double) kBuf - 8.0, D));
        D2 = std::max (3.0, std::min ((double) kBuf - 8.0, D2));

        const double pos = (double) (w - 1) - D;
        float oL = hermite (bufL, mask, pos), oR = hermite (bufR, mask, pos);
        if (fade < 1.0f)
        {
            const double pos2 = (double) (w - 1) - D2;
            const float g = fade;
            oL = oL * g + hermite (bufL, mask, pos2) * (1.0f - g);
            oR = oR * g + hermite (bufR, mask, pos2) * (1.0f - g);
            fade = std::min (1.0f, fade + fadeInc);
        }
        l[i] = oL;
        r[i] = oR;
    }
    level = rmsEnv;
    outMidi = midiNow < 0.0f ? -1.0f : midiNow + shift;
}
} // namespace si::tune
