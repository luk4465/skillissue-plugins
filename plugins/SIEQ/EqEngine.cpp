#include "EqEngine.h"
#include <algorithm>

namespace si::eq
{
namespace
{
constexpr float kPi = 3.14159265358979f;
float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }

// Butterworth section Qs for 2/4/6/8th order
const float kButterQ[4][4] = {
    { 0.7071f, 0, 0, 0 },
    { 0.5412f, 1.3066f, 0, 0 },
    { 0.5176f, 0.7071f, 1.9319f, 0 },
    { 0.5098f, 0.6013f, 0.9000f, 2.5629f },
};
} // namespace

void Section::set (float gIn, float kIn, float m0In, float m1In, float m2In)
{
    g = gIn; k = kIn; m0 = m0In; m1 = m1In; m2 = m2In;
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}

float Section::magnitude (float f, float sr) const
{
    const float w = std::tan (kPi * clampf (f, 1.0f, sr * 0.4995f) / sr) / g;
    const std::complex<float> s (0.0f, w);
    const std::complex<float> den = s * s + k * s + 1.0f;
    const std::complex<float> h = m0 + m1 * s / den + m2 / den;
    return std::abs (h);
}

int designBand (const BandParams& p, float gainDb, float sr, std::array<Section, 4>& out)
{
    const float fc = clampf (p.freq, 10.0f, sr * 0.49f);
    const float t = std::tan (kPi * fc / sr);
    const float q = clampf (p.q, 0.05f, 40.0f);
    switch (p.type)
    {
        case Type::Bell:
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float k = 1.0f / (q * A);
            out[0].set (t, k, 1.0f, k * (A * A - 1.0f), 0.0f);
            return 1;
        }
        case Type::LowShelf:
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float k = 1.0f / q;
            out[0].set (t / std::sqrt (A), k, 1.0f, k * (A - 1.0f), A * A - 1.0f);
            return 1;
        }
        case Type::HighShelf:
        {
            const float A = std::pow (10.0f, gainDb / 40.0f);
            const float k = 1.0f / q;
            out[0].set (t * std::sqrt (A), k, A * A, k * (1.0f - A) * A, 1.0f - A * A);
            return 1;
        }
        case Type::LowCut:
        case Type::HighCut:
        {
            const int n = std::min (3, std::max (0, p.slope)) + 1;
            for (int i = 0; i < n; ++i)
            {
                float sq = kButterQ[n - 1][i];
                if (i == n - 1) sq *= clampf (q / 0.7071f, 0.5f, 6.0f);   // Q adds resonance to the last stage
                const float k = 1.0f / sq;
                if (p.type == Type::LowCut) out[(size_t) i].set (t, k, 1.0f, -k, -1.0f);
                else out[(size_t) i].set (t, k, 0.0f, 0.0f, 1.0f);
            }
            return n;
        }
        case Type::Notch:
        {
            const float k = 1.0f / q;
            out[0].set (t, k, 1.0f, -k, 0.0f);
            return 1;
        }
        case Type::BandPass:
        {
            const float k = 1.0f / q;
            out[0].set (t, k, 0.0f, k, 0.0f);
            return 1;
        }
        case Type::Tilt:
        {
            const float A1 = std::pow (10.0f, -gainDb / 80.0f), A2 = std::pow (10.0f, gainDb / 80.0f);
            const float k = 1.0f / 0.5f;
            out[0].set (t / std::sqrt (A1), k, 1.0f, k * (A1 - 1.0f), A1 * A1 - 1.0f);
            out[1].set (t * std::sqrt (A2), k, A2 * A2, k * (1.0f - A2) * A2, 1.0f - A2 * A2);
            return 2;
        }
    }
    return 0;
}

float bandResponseDb (const BandParams& p, float gainDb, float f, float sr)
{
    if (! p.on) return 0.0f;
    std::array<Section, 4> s;
    const int n = designBand (p, gainDb, sr, s);
    float m = 1.0f;
    for (int i = 0; i < n; ++i) m *= s[(size_t) i].magnitude (f, sr);
    return 20.0f * std::log10 (std::max (1e-6f, m));
}

void EqEngine::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    for (auto& b : bands) b.dirty = true;
    reset();
}

void EqEngine::reset()
{
    for (auto& b : bands)
    {
        for (auto& s : b.sec) s.reset();
        b.side.reset();
        b.env = 0.0f;
        b.curGain = b.p.gain;
        b.dirty = true;
    }
    soloBp.reset();
}

void EqEngine::setBand (int i, const BandParams& p)
{
    auto& b = bands[(size_t) i];
    const auto& o = b.p;
    if (o.on != p.on || o.type != p.type || o.freq != p.freq || o.gain != p.gain || o.q != p.q || o.slope != p.slope
        || o.chan != p.chan || o.dyn != p.dyn || o.thresh != p.thresh || o.target != p.target || o.tight != p.tight)
    {
        if (o.type != p.type || o.slope != p.slope || o.chan != p.chan || o.on != p.on)
            for (auto& s : b.sec) s.reset();
        b.p = p;
        b.dirty = true;
    }
}

void EqEngine::design (Band& b, float gainDb)
{
    b.nSec = designBand (b.p, gainDb, sr, b.sec);
    b.curGain = gainDb;
    if (b.p.dyn)
    {
        const float k = 1.0f / clampf (b.p.q, 0.3f, 10.0f);
        b.side.set (std::tan (kPi * clampf (b.p.freq, 10.0f, sr * 0.49f) / sr), k, 0.0f, k, 0.0f);
    }
}

void EqEngine::process (float* l, float* r, int n)
{
    // listen / solo: band-pass of the input around the soloed band
    if (solo >= 0 && solo < kBands)
    {
        const auto& p = bands[(size_t) solo].p;
        if (soloDesigned != solo || soloFreq != p.freq || soloQ != p.q)
        {
            soloBp.set (std::tan (kPi * clampf (p.freq, 10.0f, sr * 0.49f) / sr), 1.0f / clampf (p.q, 0.4f, 12.0f), 0.0f, 1.0f / clampf (p.q, 0.4f, 12.0f), 0.0f);
            soloDesigned = solo; soloFreq = p.freq; soloQ = p.q;
        }
        for (int i = 0; i < n; ++i)
        {
            l[i] = soloBp.tick (l[i], 0);
            r[i] = soloBp.tick (r[i], 1);
        }
        return;
    }
    soloDesigned = -2;

    constexpr int kChunk = 32;
    for (int start = 0; start < n; start += kChunk)
    {
        const int m = std::min (kChunk, n - start);
        for (int bi = 0; bi < kBands; ++bi)
        {
            auto& b = bands[(size_t) bi];
            if (! b.p.on) { liveGain[(size_t) bi].store (b.p.gain); continue; }

            if (b.p.dyn)
            {
                // detector: band-passed sidechain of the band's channel, peak envelope
                const float atk = std::exp (-1.0f / (0.001f * sr));
                const float rel = std::exp (-1.0f / ((b.p.tight ? 0.04f : 0.25f) * sr));
                if (b.dirty) design (b, b.curGain);
                const float* dl = keyL != nullptr ? keyL : l;   // external sidechain or the band's own input
                const float* dr = keyL != nullptr ? keyR : r;
                for (int i = start; i < start + m; ++i)
                {
                    float x;
                    switch (b.p.chan)
                    {
                        case Chan::Mid:   x = 0.5f * (dl[i] + dr[i]); break;
                        case Chan::Side:  x = 0.5f * (dl[i] - dr[i]); break;
                        case Chan::Left:  x = dl[i]; break;
                        case Chan::Right: x = dr[i]; break;
                        default:          x = std::max (std::abs (dl[i]), std::abs (dr[i])); break;
                    }
                    const float v = std::abs (b.side.tick (x, 0));
                    b.env = v > b.env ? v + atk * (b.env - v) : v + rel * (b.env - v);
                }
                const float envDb = 20.0f * std::log10 (std::max (1e-6f, b.env));
                const float amt = clampf ((envDb - b.p.thresh) / 6.0f, 0.0f, 1.0f);
                const float g = b.p.gain + (b.p.target - b.p.gain) * amt;
                if (std::abs (g - b.curGain) > 0.02f || b.dirty) { design (b, g); b.dirty = false; }
            }
            else if (b.dirty)
            {
                design (b, b.p.gain);
                b.dirty = false;
            }
            liveGain[(size_t) bi].store (b.curGain);

            const int ns = b.nSec;
            auto chain = [&] (float x, int ch)
            {
                for (int s = 0; s < ns; ++s) x = b.sec[(size_t) s].tick (x, ch);
                return x;
            };
            for (int i = start; i < start + m; ++i)
            {
                switch (b.p.chan)
                {
                    case Chan::Stereo: l[i] = chain (l[i], 0); r[i] = chain (r[i], 1); break;
                    case Chan::Mid:  { float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]); mid = chain (mid, 0); l[i] = mid + side; r[i] = mid - side; break; }
                    case Chan::Side: { float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]); side = chain (side, 1); l[i] = mid + side; r[i] = mid - side; break; }
                    case Chan::Left:  l[i] = chain (l[i], 0); break;
                    case Chan::Right: r[i] = chain (r[i], 1); break;
                }
            }
        }
    }
}
} // namespace si::eq
