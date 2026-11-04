#include "PitchEngine.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace si::pitch
{
namespace
{
constexpr float kPi = 3.14159265358979f;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass (double fc, double sr, double q)
    {
        const double w = 2.0 * 3.14159265358979 * fc / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al;
        b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
        a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
    }
    float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    const auto mid = v.begin() + (long) v.size() / 2;
    std::nth_element (v.begin(), mid, v.end());
    return *mid;
}

/** Zero-phase one-pole low-pass over [a, b). */
void smoothRun (const std::vector<float>& in, std::vector<float>& out, int a, int b, float coef)
{
    float z = in[(size_t) a];
    for (int i = a; i < b; ++i) { z = in[(size_t) i] + coef * (z - in[(size_t) i]); out[(size_t) i] = z; }
    z = out[(size_t) (b - 1)];
    for (int i = b - 1; i >= a; --i) { z = out[(size_t) i] + coef * (z - out[(size_t) i]); out[(size_t) i] = z; }
}
} // namespace

float nearestInScale (float midi, int mask)
{
    if ((mask & 0xFFF) == 0) mask = 0xFFF;
    const int base = (int) std::floor (midi);
    float best = (float) base, bestD = 1e9f;
    for (int k = base - 6; k <= base + 7; ++k)
        if (mask & (1 << (((k % 12) + 12) % 12)))
        {
            const float d = std::abs ((float) k - midi);
            if (d < bestD) { bestD = d; best = (float) k; }
        }
    return best;
}

float correctedCenter (const Note& n, const Correction& c)
{
    if (n.manual) return n.target;
    // spoken / gliding syllables (sung < 1) are moved less, so rap parts stay natural
    return n.center + (nearestInScale (n.center, c.scaleMask) - n.center) * c.center * n.sung;
}

//==============================================================================
Analysis analyse (const float* x, int n, double sr, const std::function<void (float)>& progress)
{
    Analysis A;
    A.sr = sr;
    A.hop = std::max (32, (int) std::lround (sr * 0.005));
    A.length = n;
    const int hop = A.hop;
    const int frames = n / hop + 1;
    A.midi.assign ((size_t) frames, 0.0f);
    A.rms.assign ((size_t) frames, 0.0f);

    // frame RMS (10 ms window)
    for (int f = 0; f < frames; ++f)
    {
        const int a = std::max (0, f * hop - hop), b = std::min (n, f * hop + hop);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += (double) x[i] * x[i];
        A.rms[(size_t) f] = b > a ? (float) std::sqrt (s / (b - a)) : 0.0f;
        A.peakRms = std::max (A.peakRms, A.rms[(size_t) f]);
    }
    const float gate = A.peakRms * 0.01f;   // -40 dB under the loudest frame

    // decimate to ~11-12 kHz for YIN
    const int D = sr > 60000.0 ? 8 : 4;
    const double dsr = sr / D;
    std::vector<float> d ((size_t) (n / D + 1));
    {
        Biquad f1, f2;
        f1.lowpass (dsr * 0.4, sr, 0.5412);
        f2.lowpass (dsr * 0.4, sr, 1.3066);
        for (int i = 0, k = 0; i < n; ++i)
        {
            const float y = f2.process (f1.process (x[i]));
            if (i % D == 0) d[(size_t) k++] = y;
        }
    }
    const int dn = (int) d.size();
    std::vector<double> cum ((size_t) dn + 1, 0.0);
    for (int i = 0; i < dn; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + (double) d[(size_t) i] * d[(size_t) i];

    const int W = (int) std::lround (dsr * 0.035);
    const int maxLag = (int) std::lround (dsr / 55.0), minLag = std::max (2, (int) std::lround (dsr / 1100.0));
    int order = 1;
    while ((1 << order) < 2 * W + maxLag) ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> fa ((size_t) N * 2), fb ((size_t) N * 2);
    std::vector<float> dp ((size_t) maxLag + 2);

    for (int f = 0; f < frames; ++f)
    {
        if (progress && f % 2000 == 0) progress (0.8f * (float) f / (float) frames);
        if (A.rms[(size_t) f] < gate) continue;
        const int c = f * hop / D;
        const int s = std::max (0, std::min (dn - (W + maxLag) - 1, c - (W + maxLag) / 2));
        if (s < 0) continue;
        std::fill (fa.begin(), fa.end(), 0.0f);
        std::fill (fb.begin(), fb.end(), 0.0f);
        for (int j = 0; j < W; ++j) fa[(size_t) j] = d[(size_t) (s + j)];
        for (int j = 0; j < W + maxLag; ++j) fb[(size_t) j] = d[(size_t) (s + j)];
        fft.performRealOnlyForwardTransform (fa.data());
        fft.performRealOnlyForwardTransform (fb.data());
        for (int k = 0; k <= N / 2; ++k)
        {
            const float ar = fa[(size_t) 2 * k], ai = fa[(size_t) 2 * k + 1], br = fb[(size_t) 2 * k], bi = fb[(size_t) 2 * k + 1];
            fa[(size_t) 2 * k] = ar * br + ai * bi;       // conj(A) * B
            fa[(size_t) 2 * k + 1] = ar * bi - ai * br;
        }
        fft.performRealOnlyInverseTransform (fa.data());
        // YIN difference + cumulative mean normalisation
        const double e0 = cum[(size_t) (s + W)] - cum[(size_t) s];
        double run = 0.0;
        dp[0] = 1.0f;
        for (int t = 1; t <= maxLag; ++t)
        {
            const double et = cum[(size_t) (s + t + W)] - cum[(size_t) (s + t)];
            const double dt = std::max (0.0, e0 + et - 2.0 * fa[(size_t) t]);
            run += dt;
            dp[(size_t) t] = run > 0.0 ? (float) (dt * t / run) : 1.0f;
        }
        int tau = -1;
        for (int t = minLag; t < maxLag; ++t)
            if (dp[(size_t) t] < 0.15f)
            {
                while (t + 1 < maxLag && dp[(size_t) t + 1] < dp[(size_t) t]) ++t;
                tau = t;
                break;
            }
        if (tau < 0)
        {
            int bt = minLag;
            for (int t = minLag; t < maxLag; ++t) if (dp[(size_t) t] < dp[(size_t) bt]) bt = t;
            if (dp[(size_t) bt] < 0.3f) tau = bt;
        }
        if (tau <= minLag || tau >= maxLag) continue;
        const float y0 = dp[(size_t) tau - 1], y1 = dp[(size_t) tau], y2 = dp[(size_t) tau + 1];
        const float den = y0 - 2.0f * y1 + y2;
        const float t = (float) tau + (std::abs (den) > 1e-9f ? 0.5f * (y0 - y2) / den : 0.0f);
        const float hz = (float) (dsr / t);
        A.midi[(size_t) f] = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    }

    auto& m = A.midi;
    // octave jumps against the local median, then a 5-frame median
    {
        std::vector<float> fixedM = m;
        for (int f = 0; f < frames; ++f)
        {
            if (m[(size_t) f] <= 0.0f) continue;
            std::vector<float> nb;
            for (int k = std::max (0, f - 12); k <= std::min (frames - 1, f + 12); ++k) if (m[(size_t) k] > 0.0f) nb.push_back (m[(size_t) k]);
            const float med = median (nb);
            const float diff = m[(size_t) f] - med;
            if (std::abs (std::abs (diff) - 12.0f) < 1.5f) fixedM[(size_t) f] = m[(size_t) f] - 12.0f * (diff > 0 ? 1.0f : -1.0f);
        }
        m = fixedM;
        for (int f = 2; f + 2 < frames; ++f)
        {
            bool all = true;
            std::vector<float> w;
            for (int k = f - 2; k <= f + 2; ++k) { all = all && m[(size_t) k] > 0.0f; w.push_back (fixedM[(size_t) k]); }
            if (all) m[(size_t) f] = median (w);
        }
    }
    // fill dropouts up to 20 ms inside voiced audio, drop voiced islands shorter than 20 ms
    for (int f = 1; f < frames; ++f)
        if (m[(size_t) f] <= 0.0f && m[(size_t) f - 1] > 0.0f)
        {
            int g = f;
            while (g < frames && m[(size_t) g] <= 0.0f) ++g;
            if (g < frames && g - f <= 4 && std::abs (m[(size_t) g] - m[(size_t) f - 1]) < 2.0f)
                for (int k = f; k < g; ++k)
                    m[(size_t) k] = m[(size_t) f - 1] + (m[(size_t) g] - m[(size_t) f - 1]) * (float) (k - f + 1) / (float) (g - f + 1);
            f = g;
        }
    for (int f = 0; f < frames;)
    {
        if (m[(size_t) f] <= 0.0f) { ++f; continue; }
        int g = f;
        while (g < frames && m[(size_t) g] > 0.0f) ++g;
        if (g - f < 4) for (int k = f; k < g; ++k) m[(size_t) k] = 0.0f;
        f = g;
    }

    // slow pitch (drift) per voiced run: ~3 Hz zero-phase low-pass
    A.smooth = m;
    const float frameRate = (float) (sr / hop);
    const float coef = std::exp (-2.0f * kPi * 3.0f / frameRate);
    std::vector<std::pair<int, int>> runs;
    for (int f = 0; f < frames;)
    {
        if (m[(size_t) f] <= 0.0f) { ++f; continue; }
        int g = f;
        while (g < frames && m[(size_t) g] > 0.0f) ++g;
        runs.push_back ({ f, g });
        smoothRun (m, A.smooth, f, g, coef);
        f = g;
    }
    if (progress) progress (0.85f);

    // notes: split a voiced run where the pitch leaves the running note centre for > 50 ms.
    // The split curve is a faster (~7 Hz) low-pass so vibrato is mostly gone but slides stay sharp;
    // the cut goes to the steepest point of the slide and the new note's centre only counts settled frames.
    std::vector<float> seg = m;
    const float segCoef = std::exp (-2.0f * kPi * 7.0f / frameRate);
    for (auto [r0, r1] : runs) smoothRun (m, seg, r0, r1, segCoef);
    for (auto [r0, r1] : runs)
    {
        // optimal piecewise-constant fit of the pitch curve (dynamic programming): each extra note costs
        // kNotePenalty, so a split only happens where a new level explains the singing clearly better.
        // Vibrato swings around one centre (splitting it gains little), runs and jumps step to new levels (big gain).
        // splitting a +-0.8 st vibrato into half-cycles gains ~4.7, a 1 st step between 80 ms notes ~8, fast runs 20+
        constexpr float kNotePenalty = 6.0f;
        constexpr int minLen = 8, maxLen = 600;
        const int N = r1 - r0;
        std::vector<double> s1 ((size_t) N + 1, 0.0), s2 ((size_t) N + 1, 0.0);
        for (int i = 0; i < N; ++i)
        {
            const double v = m[(size_t) (r0 + i)];
            s1[(size_t) i + 1] = s1[(size_t) i] + v;
            s2[(size_t) i + 1] = s2[(size_t) i] + v * v;
        }
        auto sse = [&] (int a, int b) { const double k = b - a, s = s1[(size_t) b] - s1[(size_t) a]; return s2[(size_t) b] - s2[(size_t) a] - s * s / k; };
        std::vector<double> best ((size_t) N + 1, 1e30);
        std::vector<int> from ((size_t) N + 1, 0);
        best[0] = 0.0;
        for (int i = 1; i <= N; ++i)
        {
            for (int j = std::max (0, i - maxLen); j <= i - 1; ++j)
            {
                if (i - j < minLen && ! (j == 0 && i == N)) continue;
                if (best[(size_t) j] >= 1e29) continue;
                const double c = best[(size_t) j] + sse (j, i) + kNotePenalty;
                if (c < best[(size_t) i]) { best[(size_t) i] = c; from[(size_t) i] = j; }
            }
            if (best[(size_t) i] >= 1e29) { best[(size_t) i] = best[(size_t) i - 1]; from[(size_t) i] = from[(size_t) i - 1]; }
        }
        std::vector<int> cuts;
        for (int i = N; i > 0; i = from[(size_t) i]) cuts.push_back (r0 + from[(size_t) i]);
        std::reverse (cuts.begin(), cuts.end());
        if (cuts.empty() || cuts.front() != r0) cuts.insert (cuts.begin(), r0);
        // neighbours that ended up on the same pitch are one note
        for (size_t i = 1; i < cuts.size();)
        {
            const int a0 = cuts[i - 1], b0 = cuts[i], b1 = i + 1 < cuts.size() ? cuts[i + 1] : r1;
            const double ma = (s1[(size_t) (b0 - r0)] - s1[(size_t) (a0 - r0)]) / (b0 - a0), mb = (s1[(size_t) (b1 - r0)] - s1[(size_t) (b0 - r0)]) / (b1 - b0);
            const int shorter = std::min (b0 - a0, b1 - b0);
            if (std::abs (ma - mb) < 0.35 || (std::abs (ma - mb) < 0.7 && shorter < 30)) cuts.erase (cuts.begin() + (long) i);
            else ++i;
        }
        cuts.push_back (r1);
        // a short note whose pitch lies between both neighbours is just the slide: give each half to its neighbour
        for (bool merged = true; merged && cuts.size() > 3;)
        {
            merged = false;
            auto avg = [&] (int a, int b) { double s2 = 0; for (int k = a; k < b; ++k) s2 += seg[(size_t) k]; return (float) (s2 / std::max (1, b - a)); };
            for (size_t i = 1; i + 2 < cuts.size(); ++i)
            {
                if (cuts[i + 1] - cuts[i] >= 16) continue;
                const float l = avg (cuts[i - 1], cuts[i]), me = avg (cuts[i], cuts[i + 1]), r = avg (cuts[i + 1], cuts[i + 2]);
                // still gliding through its middle half (no plateau) = part of a slide, not a sung note
                const int q = (cuts[i + 1] - cuts[i]) / 4;
                const float midMove = std::abs (m[(size_t) (cuts[i + 1] - q - 1)] - m[(size_t) (cuts[i] + q)]);
                if ((me - l) * (r - me) > 0.0f && midMove > 0.45f)
                {
                    cuts[i] = (cuts[i] + cuts[i + 1]) / 2;
                    cuts.erase (cuts.begin() + (long) i + 1);
                    merged = true;
                    break;
                }
            }
        }
        // merge notes shorter than 60 ms into the neighbour with the closer pitch
        for (bool merged = true; merged && cuts.size() > 2;)
        {
            merged = false;
            for (size_t i = 0; i + 1 < cuts.size(); ++i)
                if (cuts[i + 1] - cuts[i] < 12)
                {
                    auto avg = [&] (int a, int b) { double s2 = 0; for (int k = a; k < b; ++k) s2 += A.smooth[(size_t) k]; return (float) (s2 / std::max (1, b - a)); };
                    const float me = avg (cuts[i], cuts[i + 1]);
                    const bool hasL = i > 0, hasR = i + 2 < cuts.size();
                    const float dl = hasL ? std::abs (avg (cuts[i - 1], cuts[i]) - me) : 1e9f;
                    const float dr = hasR ? std::abs (avg (cuts[i + 1], cuts[i + 2]) - me) : 1e9f;
                    if (! hasL && ! hasR) break;
                    cuts.erase (cuts.begin() + (long) (dl <= dr ? i : i + 1));
                    merged = true;
                    break;
                }
        }
        for (size_t i = 0; i + 1 < cuts.size(); ++i)
        {
            const int a = cuts[i], b = cuts[i + 1];
            if (b - a < 6) continue;
            const int trim = (b - a) * 15 / 100;
            std::vector<float> v (m.begin() + a + trim, m.begin() + b - trim);
            // the part the ear hears as "the note": frames where the (vibrato-free) pitch holds still
            std::vector<float> stable;
            for (int f = a + 1; f < b - 1; ++f)
                if (std::abs (seg[(size_t) f + 1] - seg[(size_t) f - 1]) * 0.5f < 0.04f) stable.push_back (m[(size_t) f]);
            const float stableFrac = (float) stable.size() / (float) (b - a);
            const float lenMs = (float) (b - a) * (float) hop * 1000.0f / (float) sr;
            auto smoothstep = [] (float e0, float e1, float x) { const float t = std::min (1.0f, std::max (0.0f, (x - e0) / (e1 - e0))); return t * t * (3.0f - 2.0f * t); };
            Note note;
            note.start = std::max (0, a * hop - hop / 2);
            note.end = std::min (n, b * hop - hop / 2);
            note.center = stableFrac >= 0.3f ? median (stable) : median (v);
            note.target = note.center;
            // spoken syllables glide one way through their core; vibrato swings around the centre, held / fast notes sit
            double sx = 0, sy = 0, sxx = 0, sxy = 0;
            const int c0 = a + trim, c1 = b - trim;
            for (int f = c0; f < c1; ++f) { const double t = f - c0; sx += t; sy += m[(size_t) f]; sxx += t * t; sxy += t * m[(size_t) f]; }
            const double cn = c1 - c0, den = cn * sxx - sx * sx;
            const float travel = den > 0.0 ? (float) std::abs ((cn * sxy - sx * sy) / den * cn) : 0.0f;
            note.sung = (1.0f - smoothstep (0.5f, 1.2f, travel)) * smoothstep (50.0f, 100.0f, lenMs);
            A.notes.push_back (note);
        }
    }
    if (progress) progress (0.92f);

    // pitch marks: one per period on voiced audio, every 4 ms elsewhere. Peaks are picked on a copy low-passed
    // just above the run's fundamental, so there is one clear peak per cycle and marks don't hop between harmonics.
    const int uvStep = std::max (8, (int) std::lround (sr * 0.004));
    auto periodAt = [&] (int s) { const float mm = m[(size_t) A.frameOf (s)]; return mm > 0.0f ? (float) (sr / (440.0 * std::pow (2.0, (mm - 69.0) / 12.0))) : 0.0f; };
    std::vector<float> xf (x, x + n);
    for (auto [r0, r1] : runs)
    {
        std::vector<float> v (m.begin() + r0, m.begin() + r1);
        const float f0 = (float) (440.0 * std::pow (2.0, (median (v) - 69.0) / 12.0));
        const int a = std::max (0, r0 * hop - 2 * hop), b = std::min (n, r1 * hop + 2 * hop);
        Biquad lp;
        lp.lowpass (std::min (1200.0, std::max (70.0, 1.3 * f0)), sr, 0.7071);
        for (int i = a; i < b; ++i) xf[(size_t) i] = lp.process (x[i]);
        lp.z1 = lp.z2 = 0.0f;
        for (int i = b - 1; i >= a; --i) xf[(size_t) i] = lp.process (xf[(size_t) i]);   // zero phase
    }
    auto peakIn = [&] (int a, int b) { a = std::max (0, a); b = std::min (n - 1, b); int best = a; for (int i = a; i <= b; ++i) if (xf[(size_t) i] > xf[(size_t) best]) best = i; return best; };
    int pos = 0;
    size_t ri = 0;
    while (pos < n)
    {
        const int voicedStart = ri < runs.size() ? runs[ri].first * hop : n;
        // unvoiced fill up to the next voiced run
        while (pos < voicedStart - uvStep / 2)
        {
            if (A.marks.empty() || pos - A.marks.back() >= uvStep / 2) { A.marks.push_back (pos); A.markVoiced.push_back (0); }
            pos += uvStep;
        }
        if (ri >= runs.size()) break;
        const int voicedEnd = std::min (n, runs[ri].second * hop);
        float P = periodAt (voicedStart + hop);
        int mk = peakIn (voicedStart, voicedStart + (int) P);
        if (! A.marks.empty() && mk - A.marks.back() < uvStep / 2) mk = A.marks.back() + uvStep / 2;
        while (mk < voicedEnd)
        {
            A.marks.push_back (mk);
            A.markVoiced.push_back (1);
            P = periodAt (mk);
            if (P <= 0.0f) P = (float) uvStep;
            const int exp = mk + (int) std::lround (P);
            const int nx = peakIn (exp - (int) (P * 0.2f), exp + (int) (P * 0.2f));
            mk = std::max (mk + std::max (4, (int) (P * 0.6f)), nx);
        }
        pos = A.marks.back() + uvStep;
        ++ri;
    }
    if (A.marks.empty() || A.marks.front() != 0) { A.marks.insert (A.marks.begin(), 0); A.markVoiced.insert (A.markVoiced.begin(), 0); }
    if (progress) progress (1.0f);
    return A;
}

//==============================================================================
std::vector<float> offsetCurve (const Analysis& A, const std::vector<Note>& notes, const Correction& c)
{
    const int frames = (int) A.midi.size();
    std::vector<float> shift ((size_t) frames, 0.0f), off ((size_t) frames, 0.0f);
    std::vector<char> inNote ((size_t) frames, 0);
    // note shifts, linearly bridged across short gaps between notes
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& nt = notes[i];
        const float s = correctedCenter (nt, c) - nt.center;
        const int a = A.frameOf (nt.start), b = A.frameOf (nt.end);
        for (int f = a; f <= b; ++f) { shift[(size_t) f] = s; inNote[(size_t) f] = 1; }
        if (i + 1 < notes.size())
        {
            const int g0 = b + 1, g1 = A.frameOf (notes[i + 1].start);
            const float s2 = correctedCenter (notes[i + 1], c) - notes[i + 1].center;
            if (g1 > g0 && g1 - g0 <= 20)
                for (int f = g0; f < g1; ++f) shift[(size_t) f] = s + (s2 - s) * (float) (f - g0 + 1) / (float) (g1 - g0 + 1);
        }
    }
    // soften the steps between notes (~45 ms) so slides stay natural
    std::vector<float> sm = shift;
    for (int f = 0; f < frames; ++f)
    {
        double s = 0.0; int k = 0;
        for (int j = std::max (0, f - 4); j <= std::min (frames - 1, f + 4); ++j) { s += shift[(size_t) j]; ++k; }
        sm[(size_t) f] = (float) (s / k);
    }
    // drift / vibrato inside notes
    for (const auto& nt : notes)
        for (int f = A.frameOf (nt.start); f <= A.frameOf (nt.end); ++f)
        {
            const float mm = A.midi[(size_t) f];
            if (mm <= 0.0f) continue;
            const float drift = A.smooth[(size_t) f] - nt.center, vib = mm - A.smooth[(size_t) f];
            off[(size_t) f] = -c.drift * (nt.manual ? 1.0f : nt.sung) * drift + (c.vibrato - 1.0f) * vib;
        }
    for (int f = 0; f < frames; ++f) off[(size_t) f] += sm[(size_t) f];
    return off;
}

bool isIdentity (const std::vector<float>& off)
{
    for (float v : off) if (std::abs (v) > 1e-4f) return false;
    return true;
}

//==============================================================================
void render (const Analysis& A, const std::vector<float>& off, const float* const* in, float* const* out, int numCh)
{
    const int n = A.length;
    const auto& mk = A.marks;
    const int M = (int) mk.size();
    for (int ch = 0; ch < numCh; ++ch) std::fill (out[ch], out[ch] + n, 0.0f);
    if (M < 2 || n <= 0) return;
    std::vector<float> wsum ((size_t) n, 0.0f);
    const int frames = (int) off.size();
    auto ratioAt = [&] (double t)
    {
        const double fpos = t / A.hop;
        const int f = std::min (frames - 1, std::max (0, (int) fpos));
        const int g = std::min (frames - 1, f + 1);
        const float fr = (float) (fpos - f);
        const float o = off[(size_t) f] + (off[(size_t) g] - off[(size_t) f]) * std::min (1.0f, std::max (0.0f, fr));
        return std::pow (2.0f, o / 12.0f);
    };

    double ts = mk[0];
    int a = 0;
    double prevStep = mk[1] - mk[0];
    while (ts < n)
    {
        while (a + 1 < M && std::abs (mk[(size_t) a + 1] - ts) <= std::abs (mk[(size_t) a] - ts)) ++a;
        const int ap = mk[(size_t) a];
        const int La = a > 0 ? ap - mk[(size_t) a - 1] : (a + 1 < M ? mk[(size_t) a + 1] - ap : A.hop);
        const int Ra = a + 1 < M ? mk[(size_t) a + 1] - ap : La;
        const float r = A.markVoiced[(size_t) a] ? ratioAt (ts) : 1.0f;
        const double step = std::max (2.0, Ra / (double) r);
        const double L = std::max ((double) La, prevStep), R = std::max ((double) Ra, step);
        const int tsi = (int) std::floor (ts);
        const double frac = ts - tsi;
        const int j0 = (int) std::ceil (-L + frac), j1 = (int) std::floor (R + frac);
        for (int j = j0; j <= j1; ++j)
        {
            const int o = tsi + j;
            if (o < 0 || o >= n) continue;
            const double u = j - frac;   // offset from the grain centre
            const float w = u < 0.0 ? 0.5f + 0.5f * std::cos (kPi * (float) (u / L)) : 0.5f + 0.5f * std::cos (kPi * (float) (u / R));
            if (w <= 0.0f) continue;
            const double src = ap + u;
            const int si0 = (int) std::floor (src);
            const float sf = (float) (src - si0);
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x0 = si0 >= 0 && si0 < n ? in[ch][si0] : 0.0f;
                const float x1 = si0 + 1 >= 0 && si0 + 1 < n ? in[ch][si0 + 1] : 0.0f;
                out[ch][o] += w * (x0 + (x1 - x0) * sf);
            }
            wsum[(size_t) o] += w;
        }
        prevStep = step;
        ts += step;
    }
    for (int i = 0; i < n; ++i)
    {
        const float g = wsum[(size_t) i] > 1e-3f ? 1.0f / wsum[(size_t) i] : 0.0f;
        for (int ch = 0; ch < numCh; ++ch) out[ch][i] *= g;
    }
}
} // namespace si::pitch
