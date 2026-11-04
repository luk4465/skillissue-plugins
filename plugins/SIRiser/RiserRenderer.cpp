#include "RiserRenderer.h"
#include "dsp/ReverbEngine.h"

using namespace si::dsp;

namespace si
{
namespace
{
constexpr double kFeedBeats[] = { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 0.0 };

bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load(); }

/** Auto feed length: from onset to the first ~80 ms gap (the first word / phrase), 80 ms..2.5 s. */
int autoFeedLength (const juce::AudioBuffer<float>& src, int onset, double sr)
{
    const int n = src.getNumSamples();
    float peak = 0.0f;
    for (int ch = 0; ch < src.getNumChannels(); ++ch)
        peak = std::max (peak, src.getMagnitude (ch, onset, n - onset));
    const float thr = peak * 0.05f;
    const float rel = std::exp (-1.0f / (0.01f * (float) sr));
    const int minLen = (int) (0.08 * sr), maxLen = (int) (2.5 * sr), gapLen = (int) (0.08 * sr);
    float env = 0.0f;
    int quiet = 0;
    for (int i = onset; i < n; ++i)
    {
        float v = 0.0f;
        for (int ch = 0; ch < src.getNumChannels(); ++ch)
            v = std::max (v, std::abs (src.getSample (ch, i)));
        env = v > env ? v : v + rel * (env - v);
        quiet = env < thr ? quiet + 1 : 0;
        const int len = i - onset;
        if (len > minLen && quiet > gapLen) return len - quiet + gapLen / 2;
        if (len >= maxLen) return maxLen;
    }
    return std::max (minLen, n - onset);
}

/** Automatic sibilance detection: ducks the > 5.5 kHz band when it dominates, so "S" sounds don't smear into the riser. */
void deEss (juce::AudioBuffer<float>& b, float amount, double sr)
{
    if (amount <= 0.0f) return;
    const float fs = (float) sr;
    Svf split[2];
    for (auto& s : split) s.set (5500.0f, 0.707f, fs);
    const float aF = std::exp (-1.0f / (0.001f * fs)), rF = std::exp (-1.0f / (0.06f * fs));
    float envHf = 0.0f, envAll = 0.0f, gainSm = 1.0f;
    const int nCh = std::min (2, b.getNumChannels());
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        float hf[2] = {}, x[2] = {};
        float lvHf = 0.0f, lvAll = 0.0f;
        for (int ch = 0; ch < nCh; ++ch)
        {
            x[ch] = b.getSample (ch, i);
            float l, bp, h;
            split[ch].tick (x[ch], l, bp, h);
            hf[ch] = h;
            lvHf = std::max (lvHf, std::abs (h));
            lvAll = std::max (lvAll, std::abs (x[ch]));
        }
        envHf = lvHf > envHf ? lvHf + aF * (envHf - lvHf) : lvHf + rF * (envHf - lvHf);
        envAll = lvAll > envAll ? lvAll + aF * (envAll - lvAll) : lvAll + rF * (envAll - lvAll);
        const float ratio = envAll > 1e-4f ? envHf / envAll : 0.0f;
        const float gr = amount * clampf ((ratio - 0.3f) / 0.3f, 0.0f, 1.0f);
        const float target = 1.0f - 0.92f * gr;
        gainSm = target < gainSm ? target : target + 0.999f * (gainSm - target);
        for (int ch = 0; ch < nCh; ++ch)
            b.setSample (ch, i, (x[ch] - hf[ch]) + hf[ch] * gainSm);
    }
}

inline float cubicRead (const float* d, int len, double pos)
{
    const int i = (int) pos;
    const float f = (float) (pos - i);
    auto at = [&] (int k) { return d[juce::jlimit (0, len - 1, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
} // namespace

int findOnset (const juce::AudioBuffer<float>& b)
{
    float peak = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        peak = std::max (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
    if (peak < 1e-4f) return -1;
    const float thr = peak * 0.0316f; // -30 dB re peak
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            if (std::abs (b.getSample (ch, i)) > thr)
                return std::max (0, i - 32);
    return -1;
}

std::unique_ptr<RiserResult> renderRiser (const RiserSettings& s, const juce::AudioBuffer<float>& source, double sr, const std::atomic<bool>* cancel)
{
    juce::ScopedNoDenormals noDenormals;
    if (source.getNumSamples() < 64 || source.getNumChannels() < 1) return nullptr;
    const int onset = findOnset (source);
    if (onset < 0) return nullptr;

    const float fs = (float) sr;
    const double bpm = juce::jlimit (20.0, 400.0, s.bpm);
    const double beatSec = 60.0 / bpm;
    const int L = std::max (64, (int) std::round (s.lengthBeats * beatSec * sr));
    const int srcLen = source.getNumSamples() - onset;

    // ---- feed: the part of the source that excites the reverb
    int feedLen = srcLen;
    if (s.feed == 0) feedLen = autoFeedLength (source, onset, sr);
    else if (s.feed >= 1 && s.feed <= 5) feedLen = (int) (kFeedBeats[s.feed] * beatSec * sr);
    feedLen = juce::jlimit (32, std::min (srcLen, (int) (20.0 * sr)), feedLen);

    juce::AudioBuffer<float> feed (2, feedLen);
    for (int ch = 0; ch < 2; ++ch)
        feed.copyFrom (ch, 0, source, std::min (ch, source.getNumChannels() - 1), onset, feedLen);
    const int fadeOut = std::min (feedLen / 4, (int) (0.015 * sr));
    for (int ch = 0; ch < 2; ++ch)
        feed.applyGainRamp (ch, feedLen - fadeOut, fadeOut, 1.0f, 0.0f);
    deEss (feed, s.deEss, sr);
    if (cancelled (cancel)) return nullptr;

    // ---- reversed domain: [reversed feed][tail space]
    const int T = L + (int) (0.3 * sr);
    const int N = feedLen + T;
    juce::AudioBuffer<float> work (2, N);
    work.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* f = feed.getReadPointer (ch);
        float* w = work.getWritePointer (ch);
        for (int i = 0; i < feedLen; ++i) w[i] = f[feedLen - 1 - i];
    }

    if (s.echo)   // ping-pong echo before the reverb -> reverse echoes lead into the drop
    {
        const int d = std::max (1, (int) (s.fxBeats * 3.0 * beatSec * sr * 0.5)); // dotted-ish feel
        std::vector<float> lineL ((size_t) d, 0.0f), lineR ((size_t) d, 0.0f);
        float* wl = work.getWritePointer (0);
        float* wr = work.getWritePointer (1);
        for (int i = 0, p = 0; i < N; ++i, p = (p + 1) % d)
        {
            const float dl = lineL[(size_t) p], dr = lineR[(size_t) p];
            lineL[(size_t) p] = wl[i] + 0.5f * dr;
            lineR[(size_t) p] = wr[i] + 0.5f * dl;
            wl[i] += 0.6f * dl;
            wr[i] += 0.6f * dr;
        }
    }

    ReverbEngine verb;
    verb.prepare (sr, 512);
    ReverbParams vp;
    vp.mode = s.verbMode;
    vp.color = s.verbColor;
    vp.size = s.size;
    vp.decay = std::max (0.3f, s.tail * (float) L / fs);
    vp.attack = 0.15f;
    vp.earlyDiff = 0.85f;
    vp.lateDiff = 0.8f;
    vp.modRate = 0.5f;
    vp.modDepth = 0.45f;
    vp.highCut = s.hiCut;
    vp.lowCut = s.loCut;
    vp.bassMult = 1.0f;
    vp.highShelf = -3.0f;
    vp.highFreq = 7000.0f;
    vp.preDelayMs = 0.0f;
    vp.width = 1.0f;
    verb.setParams (vp);
    for (int pos = 0; pos < N; pos += 512)
    {
        const int n = std::min (512, N - pos);
        verb.process (work.getWritePointer (0, pos), work.getWritePointer (1, pos), n);
        if ((pos & 0xFFFF) == 0 && cancelled (cancel)) return nullptr;
    }

    // back to forward time: fwd[j] = work[N-1-j]; feed onset sits at index T
    juce::AudioBuffer<float> fwd (2, T);
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* w = work.getReadPointer (ch);
        float* o = fwd.getWritePointer (ch);
        for (int j = 0; j < T; ++j) o[j] = w[N - 1 - j];
    }

    // ---- pitch rise: variable-rate read that lands exactly on the drop
    juce::AudioBuffer<float> riser (2, L);
    {
        std::vector<double> phase ((size_t) L + 1);
        double acc = 0.0;
        for (int k = 0; k < L; ++k)
        {
            phase[(size_t) k] = acc;
            const double t = (double) k / L;
            acc += std::pow (2.0, s.pitch * (1.0 - t) / 12.0);
        }
        const double startPos = (double) T - acc;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = fwd.getReadPointer (ch);
            float* o = riser.getWritePointer (ch);
            for (int k = 0; k < L; ++k) o[k] = cubicRead (src, T, startPos + phase[(size_t) k]);
        }
    }
    if (cancelled (cancel)) return nullptr;

    // ---- shape + FX over normalised time t
    Svf filt[2], phHp[2], phLp[2];
    for (int ch = 0; ch < 2; ++ch) { phHp[ch].set (380.0f, 0.8f, fs); phLp[ch].set (3200.0f, 0.9f, fs); }
    const int chorusMax = (int) (0.03 * sr) + 8;
    std::vector<float> chBuf[2] = { std::vector<float> ((size_t) chorusMax, 0.0f), std::vector<float> ((size_t) chorusMax, 0.0f) };
    int chW = 0;
    float holdL = 0.0f, holdR = 0.0f;
    int holdCount = 0;
    const int holdN = std::max (1, (int) (sr / 11025.0));
    const double fxHz = 1.0 / (s.fxBeats * beatSec);
    double trPhase = 0.0, stPhase = 0.0, chPhase = 0.0;
    float gateSm = 1.0f;
    const float gateA = std::exp (-1.0f / (0.002f * fs));
    const float shapeExp = std::pow (2.0f, -2.0f * clampf (s.shape, -1.0f, 1.0f));

    float* oL = riser.getWritePointer (0);
    float* oR = riser.getWritePointer (1);
    for (int k = 0; k < L; ++k)
    {
        const float t = (float) k / (float) L;
        float l = oL[k], r = oR[k];

        if (s.filter > 0.0f && (k & 31) == 0)
        {
            float hz = 20000.0f, q = 0.75f;
            if (s.filterType == 0) { const float f0 = 20000.0f * std::pow (0.01f, s.filter); hz = f0 * std::pow (20000.0f / f0, std::pow (t, 1.6f)); }
            else if (s.filterType == 1) { const float f0 = 20.0f * std::pow (200.0f, s.filter); hz = f0 * std::pow (20.0f / f0, std::pow (t, 0.7f)); }
            else { hz = 250.0f * std::pow (40.0f, t); q = 0.8f + 3.0f * s.filter; }
            for (auto& f : filt) f.set (hz, q, fs);
        }
        if (s.filter > 0.0f)
        {
            float lo, bp, hi;
            const float mixF = s.filterType == 2 ? s.filter : 1.0f;
            filt[0].tick (l, lo, bp, hi);
            const float yl = s.filterType == 0 ? lo : (s.filterType == 1 ? hi : bp * 1.6f);
            filt[1].tick (r, lo, bp, hi);
            const float yr = s.filterType == 0 ? lo : (s.filterType == 1 ? hi : bp * 1.6f);
            l = l + (yl - l) * mixF;
            r = r + (yr - r) * mixF;
        }

        if (s.phone)
        {
            l = softClip (phLp[0].lowpass (phHp[0].highpass (l)) * 2.2f) * 0.7f;
            r = softClip (phLp[1].lowpass (phHp[1].highpass (r)) * 2.2f) * 0.7f;
        }

        if (s.chorus)
        {
            chBuf[0][(size_t) chW] = l;
            chBuf[1][(size_t) chW] = r;
            chPhase += 0.6 / sr;
            const float m = (float) std::sin (chPhase * juce::MathConstants<double>::twoPi);
            auto tap = [&] (int ch, float delay)
            {
                double pos = (double) chW - delay * sr;
                while (pos < 0) pos += chorusMax;
                const int i0 = (int) pos;
                const float fr = (float) (pos - i0);
                const auto& b = chBuf[ch];
                return b[(size_t) i0] + (b[(size_t) ((i0 + 1) % chorusMax)] - b[(size_t) i0]) * fr;
            };
            const float cl = tap (0, 0.012f + 0.005f * m), cr = tap (1, 0.012f - 0.005f * m);
            chW = (chW + 1) % chorusMax;
            l = l * 0.7f + cr * 0.6f;
            r = r * 0.7f + cl * 0.6f;
        }

        if (s.lofi)
        {
            if (holdCount++ % holdN == 0) { holdL = l; holdR = r; }
            constexpr float q = 1.0f / 64.0f;
            l = std::round (holdL / q) * q;
            r = std::round (holdR / q) * q;
        }

        const double rateMul = s.accel ? std::pow (8.0, (double) t) * 0.5 : 1.0; // 0.5x -> 4x
        if (s.tremolo)
        {
            trPhase += fxHz * rateMul / sr;
            const float g = 1.0f - 0.85f * (0.5f - 0.5f * (float) std::cos (trPhase * juce::MathConstants<double>::twoPi));
            l *= g; r *= g;
        }
        if (s.stutter)
        {
            stPhase += fxHz * rateMul / sr;
            const float target = std::fmod (stPhase, 1.0) < 0.55 ? 1.0f : 0.0f;
            gateSm = target + gateA * (gateSm - target);
            const float g = t > 0.97f ? 1.0f : gateSm; // open right before the drop
            l *= g; r *= g;
        }

        // width build (M/S)
        const float w = s.widthStart + (s.widthEnd - s.widthStart) * t;
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * w;
        l = mid + side;
        r = mid - side;

        // shape curve
        const float g = std::pow (t, shapeExp);
        oL[k] = l * g;
        oR[k] = r * g;
    }

    // tiny fade at the very start
    const int fi = std::min (L / 8, (int) (0.01 * sr));
    for (int ch = 0; ch < 2; ++ch) riser.applyGainRamp (ch, 0, fi, 0.0f, 1.0f);

    // ---- level
    float gain = juce::Decibels::decibelsToGain (s.gainDb);
    if (s.normalize)
    {
        // peak just under the vocal's own peak, so the swell sits with the drop instead of jumping out
        const int look = std::min (srcLen, (int) (2.0 * sr));
        float srcPk = 0.0f;
        for (int ch = 0; ch < source.getNumChannels(); ++ch) srcPk = std::max (srcPk, source.getMagnitude (ch, onset, look));
        const float target = clampf (srcPk * 0.891f, 0.1f, 0.891f);
        const float pk = std::max (riser.getMagnitude (0, 0, L), riser.getMagnitude (1, 0, L));
        if (pk > 1e-6f) gain *= target / pk;
    }
    riser.applyGain (gain);

    // ---- output: riser [+ dry source from the drop (+ forward "post" verb)]
    auto res = std::make_unique<RiserResult>();
    res->sampleRate = sr;
    res->bpm = bpm;
    res->lengthBeats = s.lengthBeats;
    res->riserSamples = L;
    const int dryLen = s.includeDry ? std::min (srcLen, (int) (30.0 * sr)) : 0;
    const int postTail = (dryLen > 0 && s.post > 0.0f) ? (int) (2.0 * sr) : 0;
    res->audio.setSize (2, L + dryLen + postTail);
    res->audio.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        res->audio.copyFrom (ch, 0, riser, ch, 0, L);
        if (dryLen > 0)
        {
            res->audio.copyFrom (ch, L, source, std::min (ch, source.getNumChannels() - 1), onset, dryLen);
            res->audio.applyGainRamp (ch, L, std::min (dryLen, 64), 0.0f, 1.0f);
        }
    }
    if (postTail > 0)
    {
        // the same space continues forward after the drop, so riser and vocal sound like one room
        juce::AudioBuffer<float> wet (2, dryLen + postTail);
        wet.clear();
        for (int ch = 0; ch < 2; ++ch) wet.copyFrom (ch, 0, res->audio, ch, L, dryLen);
        ReverbEngine fwdVerb;
        fwdVerb.prepare (sr, 512);
        vp.decay = clampf (0.6f * s.tail * (float) L / fs, 0.6f, 4.0f);
        vp.preDelayMs = 25.0f;
        fwdVerb.setParams (vp);
        for (int pos = 0; pos < wet.getNumSamples(); pos += 512)
            fwdVerb.process (wet.getWritePointer (0, pos), wet.getWritePointer (1, pos), std::min (512, wet.getNumSamples() - pos));
        for (int ch = 0; ch < 2; ++ch) res->audio.addFrom (ch, L, wet, ch, 0, wet.getNumSamples(), 0.5f * s.post);
    }
    return res;
}
} // namespace si
