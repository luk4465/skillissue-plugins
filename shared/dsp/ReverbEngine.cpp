#include "ReverbEngine.h"

#if defined(_MSC_VER)
 #pragma warning(disable : 4305 4244) // double literals in the mode table
#endif

namespace si::dsp
{
// clang-format off
static const ModeDef kModes[kNumModes] = {
 // name              blurb                                   family          size  dec  inD   latD  mod              mD   mR   er    drv   hf    spr   den  nl    pre  beats
 { "Concert Hall",   "Big, lush hall with smooth chorusing",   Family::Hall,    1.00, 1.0, 0.80, 0.60, ModType::Sine,    0.6, 1.0, 0.25, 0.00, 0.15, 0.80, 0.8, false, 30, 4 },
 { "Bright Hall",    "Open hall with airy top end",            Family::Hall,    1.00, 1.0, 0.75, 0.55, ModType::Sine,    0.5, 1.0, 0.20, 0.00, 0.00, 0.90, 0.8, false, 25, 4 },
 { "Plate",          "Dense, bright classic plate",            Family::Plate,   0.80, 1.0, 0.95, 0.85, ModType::Sine,    0.3, 1.2, 0.00, 0.00, 0.05, 0.70, 1.0, false, 10, 2 },
 { "Room",           "Natural room with early reflections",    Family::Room,    1.00, 1.0, 0.70, 0.50, ModType::Random,  0.3, 0.8, 0.50, 0.00, 0.25, 0.70, 0.7, false, 12, 1 },
 { "Chamber",        "Warm echo chamber, fast build",          Family::Chamber, 1.00, 1.0, 0.85, 0.70, ModType::Random,  0.35,0.9, 0.40, 0.00, 0.20, 0.75, 0.85,false, 18, 2 },
 { "Random Space",   "Wandering, randomized delay tank",       Family::Hall,    1.10, 1.0, 0.70, 0.60, ModType::Random,  1.0, 1.0, 0.25, 0.00, 0.15, 1.00, 0.8, false, 25, 4 },
 { "Chorus Space",   "Deep chorused shimmer space",            Family::Hall,    1.00, 1.0, 0.70, 0.60, ModType::Chorus,  1.4, 1.0, 0.20, 0.00, 0.10, 1.00, 0.8, false, 25, 4 },
 { "Ambience",       "Short ambience, adds size not tail",     Family::Tiny,    1.00, 0.6, 0.80, 0.60, ModType::Random,  0.2, 1.0, 0.70, 0.00, 0.30, 0.80, 0.7, false, 5,  0.5f },
 { "Sanctuary",      "Huge space with slow, soft build",       Family::Huge,    1.20, 1.3, 0.90, 0.80, ModType::Sine,    0.5, 0.6, 0.10, 0.00, 0.20, 0.90, 0.9, false, 45, 8 },
 { "Dirty Hall",     "Hall with saturated, gritty loop",       Family::Hall,    1.00, 1.0, 0.75, 0.55, ModType::Random,  0.6, 1.0, 0.25, 0.35, 0.25, 0.80, 0.75,false, 25, 4 },
 { "Dirty Plate",    "Driven plate, vintage hardware grit",    Family::Plate,   0.80, 1.0, 0.95, 0.80, ModType::Sine,    0.3, 1.2, 0.00, 0.40, 0.15, 0.70, 1.0, false, 10, 2 },
 { "Smooth Plate",   "Ultra-dense plate, no flutter",          Family::Plate,   0.85, 1.0, 1.00, 0.95, ModType::Sine,    0.25,0.7, 0.00, 0.00, 0.10, 0.70, 1.0, false, 12, 2 },
 { "Smooth Room",    "Dense, even room for mixes",             Family::Room,    1.00, 1.0, 0.95, 0.85, ModType::Sine,    0.2, 0.7, 0.35, 0.00, 0.25, 0.70, 0.9, false, 12, 1 },
 { "Smooth Random",  "Dense random hall, silky tail",          Family::Hall,    1.00, 1.0, 0.95, 0.85, ModType::Random,  0.8, 0.7, 0.15, 0.00, 0.15, 1.00, 0.95,false, 25, 4 },
 { "Nonlin",         "Gated / reverse 80s effect",             Family::Tiny,    1.00, 1.0, 0.90, 0.50, ModType::Random,  0.2, 1.0, 1.00, 0.00, 0.20, 0.90, 1.0, true,  0,  0.5f },
 { "Chaotic Hall",   "Chaotic modulation, evolving hall",      Family::Hall,    1.05, 1.0, 0.80, 0.65, ModType::Chaotic, 1.0, 1.0, 0.20, 0.00, 0.15, 1.00, 0.85,false, 30, 4 },
 { "Chaotic Chamber","Chaotic modulation in a tight chamber",  Family::Chamber, 1.00, 1.0, 0.85, 0.70, ModType::Chaotic, 0.9, 1.0, 0.35, 0.00, 0.20, 0.90, 0.85,false, 18, 2 },
 { "Chaotic Neutral","Subtle chaos, transparent tail",         Family::Hall,    1.00, 1.0, 0.80, 0.65, ModType::Chaotic, 0.7, 0.6, 0.20, 0.00, 0.10, 0.90, 0.85,false, 25, 4 },
 { "Cathedral",      "Massive stone space, long pre-build",    Family::Huge,    1.30, 1.4, 0.85, 0.75, ModType::Sine,    0.4, 0.5, 0.15, 0.00, 0.25, 0.90, 0.9, false, 50, 8 },
 { "Palace",         "Wide, bright ballroom",                  Family::Huge,    1.15, 1.1, 0.80, 0.65, ModType::Random,  0.5, 0.7, 0.20, 0.00, 0.10, 1.00, 0.9, false, 40, 6 },
 { "Chamber 1979",   "Dark vintage digital chamber",           Family::Chamber, 0.95, 1.0, 0.80, 0.60, ModType::Random,  0.25,0.8, 0.45, 0.10, 0.35, 0.70, 0.75,false, 18, 2 },
 { "Hall 1984",      "Classic 80s digital hall, chorused",     Family::Hall,    1.00, 1.0, 0.80, 0.60, ModType::Chorus,  0.8, 0.8, 0.20, 0.05, 0.20, 0.85, 0.8, false, 25, 4 },
};

static const float kFamilyMs[6][8] = {
    { 43.1f, 51.7f, 61.3f, 69.9f, 79.3f, 87.1f, 97.7f, 107.3f },   // Hall
    { 17.3f, 21.1f, 24.7f, 28.9f, 33.1f, 36.7f, 41.3f, 45.1f },    // Room
    { 23.9f, 29.3f, 34.7f, 41.9f, 47.3f, 53.1f, 59.9f, 66.7f },    // Plate
    { 29.7f, 33.9f, 38.3f, 44.1f, 49.7f, 55.3f, 60.1f, 65.9f },    // Chamber
    { 71.3f, 83.1f, 97.3f, 113.9f, 127.1f, 139.7f, 157.3f, 173.9f },// Huge
    { 7.9f,  9.7f,  11.3f, 13.1f, 14.9f, 16.7f, 18.1f, 19.9f },    // Tiny
};
static const float kLineApMs[8] = { 3.1f, 4.3f, 5.3f, 6.1f, 7.3f, 8.9f, 9.7f, 11.3f };
static const float kSignL[8] = { 1, -1, 1, 1, -1, 1, -1, -1 };
// L and R output signs are orthogonal (dot product 0): uncorrelated, not anti-phase, tails
static const float kSignR[8] = { 1, 1, 1, -1, -1, -1, 1, -1 };
// clang-format on

const ModeDef& getMode (int index) { return kModes[std::min (std::max (index, 0), kNumModes - 1)]; }

static constexpr float kModDepthSec = 0.0025f;

static inline void hadamard8 (float* x)
{
    for (int h = 1; h < 8; h <<= 1)
        for (int i = 0; i < 8; i += h * 2)
            for (int j = i; j < i + h; ++j)
            {
                const float a = x[j], b = x[j + h];
                x[j] = a + b;
                x[j + h] = a - b;
            }
    for (int i = 0; i < 8; ++i)
        x[i] *= 0.35355339f;
}

void ReverbEngine::prepare (double sampleRate, int)
{
    sr = (float) sampleRate;
    const int preMax = (int) (sr * 5.1f);
    preL.allocate (preMax);
    preR.allocate (preMax);
    for (int i = 0; i < kInAP; ++i)
    {
        apL[(size_t) i].allocate ((int) (sr * 0.05f));
        apR[(size_t) i].allocate ((int) (sr * 0.05f));
    }
    erL.allocate ((int) (sr * 1.6f));
    erR.allocate ((int) (sr * 1.6f));
    for (int i = 0; i < N; ++i)
    {
        line[(size_t) i].allocate ((int) (sr * 0.32f));
        lineAp[(size_t) i].allocate ((int) (sr * 0.03f));
        lenSm[(size_t) i].setTime (0.08f, sr);
        rnd[(size_t) i] = RandomLfo ((uint32_t) (i * 31 + 5));
        lfoPhase[(size_t) i] = (float) i / (float) N;
    }
    preSm.setTime (0.08f, sr);
    dirty = true;
    updateCoefficients();
    for (int i = 0; i < N; ++i)
        lenSm[(size_t) i].reset (baseLen[(size_t) i]);
    reset();
}

void ReverbEngine::reset()
{
    preL.clear(); preR.clear(); erL.clear(); erR.clear();
    for (auto& a : apL) a.clear();
    for (auto& a : apR) a.clear();
    for (int i = 0; i < N; ++i)
    {
        line[(size_t) i].clear();
        lineAp[(size_t) i].clear();
        xover[(size_t) i].reset(); hfDamp[(size_t) i].reset(); bwLoop[(size_t) i].reset();
        walk[(size_t) i] = 0.0f;
    }
    bwL.reset(); bwR.reset(); outBwL.reset(); outBwR.reset();
    hcL.reset(); hcR.reset(); lcL.reset(); lcR.reset();
    envOut = 0.0f;
}

void ReverbEngine::setParams (const ReverbParams& p)
{
    const bool same = p.mode == prm.mode && p.color == prm.color && p.decay == prm.decay && p.size == prm.size
                   && p.attack == prm.attack && p.earlyDiff == prm.earlyDiff && p.lateDiff == prm.lateDiff
                   && p.modRate == prm.modRate && p.modDepth == prm.modDepth && p.highCut == prm.highCut
                   && p.lowCut == prm.lowCut && p.bassMult == prm.bassMult && p.bassXover == prm.bassXover
                   && p.highShelf == prm.highShelf && p.highFreq == prm.highFreq && p.preDelayMs == prm.preDelayMs
                   && p.width == prm.width && p.freeze == prm.freeze;
    if (same) return;
    prm = p;
    dirty = true;
}

void ReverbEngine::updateCoefficients()
{
    const ModeDef& m = getMode (prm.mode);
    const auto color = (Color) std::min (2, std::max (0, prm.color));
    const float size = clampf (prm.size, 0.0f, 1.0f);
    const float scale = (0.12f + 0.88f * size) * m.sizeMul;
    const float rt = std::max (0.05f, prm.decay * m.decayMul);
    const float rtLow = rt * clampf (prm.bassMult, 0.1f, 6.0f);

    // colour
    float bw = 20000.0f, loopBw = 19000.0f, modScale = 1.0f;
    quantStep = 0.0f;
    if (color == Color::Seventies) { bw = 7500.0f; loopBw = 7000.0f; modScale = 0.3f; quantStep = 1.0f / 2048.0f; }
    else if (color == Color::Eighties) { bw = 11000.0f; loopBw = 10500.0f; modScale = 0.85f; quantStep = 1.0f / 16384.0f; }
    loopBw *= (1.0f - 0.45f * m.hfDamp);
    loopBwOn = color != Color::Now;
    bwL.setCutoff (bw, sr); bwR.setCutoff (bw, sr);
    outBwL.setCutoff (bw, sr); outBwR.setCutoff (bw, sr);

    preSm.target = clampf (prm.preDelayMs, 0.0f, 5000.0f) * 0.001f * sr;

    for (int i = 0; i < kInAP; ++i)
    {
        const float d = apBaseMs[(size_t) i] * (0.5f + 0.6f * size) * 0.001f * sr;
        const float g = clampf (0.75f * prm.earlyDiff * m.inDiff, 0.0f, 0.8f);
        apL[(size_t) i].delay = d;          apL[(size_t) i].g = g;
        apR[(size_t) i].delay = d * 1.137f; apR[(size_t) i].g = g;
    }

    // attack smear / ER
    const float att = clampf (prm.attack, 0.0f, 1.0f);
    const float win = (6.0f + 24.0f * size + att * 260.0f * (0.4f + 0.6f * size)) * 0.001f * sr;
    float sum = 0.0f;
    Rng r (17 + (uint32_t) prm.mode);
    for (int k = 0; k < kErTaps; ++k)
    {
        const float t = (float) (k + 1) / (float) kErTaps;
        erTimeL[(size_t) k] = std::max (2.0f, win * std::pow (t, 1.25f) * (1.0f + 0.07f * r.next()));
        erTimeR[(size_t) k] = std::max (2.0f, win * std::pow (t, 1.25f) * (1.0f + 0.07f * r.next()) * (1.0f + 0.05f * m.spread));
        const float rising = std::pow (t, 2.5f * att);
        const float falling = std::pow (1.0f - 0.85f * t, 1.5f);
        erGain[(size_t) k] = att * rising + (1.0f - att) * falling;
        sum += erGain[(size_t) k];
    }
    for (auto& g : erGain) g /= std::max (0.001f, sum) * 0.75f;
    erOut = m.erMix;

    // nonlin gate
    if (m.nonlin)
    {
        const float gate = clampf (prm.decay, 0.05f, 1.5f) * sr;
        float s2 = 0.0f;
        for (int k = 0; k < kNlTaps; ++k)
        {
            const float t = (float) (k + 1) / (float) kNlTaps;
            nlTime[(size_t) k] = std::max (2.0f, gate * t * (1.0f + 0.03f * r.next()));
            // attack 0 = gated flat, 0.5 = falling, 1 = reverse (rising)
            const float flat = 1.0f, fall = 1.0f - 0.9f * t, rise = 0.15f + 0.85f * t * t;
            const float a2 = att * 2.0f;
            nlGain[(size_t) k] = a2 < 1.0f ? flat + (fall - flat) * a2 : fall + (rise - fall) * (a2 - 1.0f);
            s2 += nlGain[(size_t) k];
        }
        for (auto& g : nlGain) g *= 6.5f / std::max (0.001f, s2);
    }

    // tank
    drive = m.drive;
    const float chorus = m.mod == ModType::Chorus ? 1.6f : 1.0f;
    // depth scale matched to VintageVerb by how fast the tail's fine spectral structure changes (a static tail sounds metallic)
    modSamples = clampf (prm.modDepth, 0.0f, 1.0f) * m.modDepthMul * modScale * chorus * kModDepthSec * sr;
    chorusOut = m.mod == ModType::Chorus ? 0.5f : 0.0f;
    const float hsDb = clampf (prm.highShelf, -24.0f, 0.0f);
    // like VintageVerb: HighShelf = extra attenuation above HighFreq over one decay time (-24 dB: highs ring ~70 % as long);
    // with NOW (no loop bandwidth filter) the mode's own damping goes in here
    const float rtHigh = rt * 60.0f / (60.0f - hsDb) * (color == Color::Now ? 1.0f - 0.3f * m.hfDamp : 1.0f);
    for (int i = 0; i < N; ++i)
    {
        const float ms = kFamilyMs[(int) m.family][i] * scale;
        baseLen[(size_t) i] = ms * 0.001f * sr;
        lineAp[(size_t) i].delay = kLineApMs[i] * (0.4f + 0.7f * size) * 0.001f * sr;
        lineAp[(size_t) i].g = clampf (0.72f * prm.lateDiff * m.lateDiff, 0.0f, 0.72f) * (i % 2 ? -1.0f : 1.0f);
        // decay gains use the whole loop: line + modulation headroom + its in-loop all-pass
        const float lenSec = (baseLen[(size_t) i] + modSamples * 2.0f + 4.0f + lineAp[(size_t) i].delay) / sr;
        gHigh[(size_t) i] = std::pow (10.0f, -3.0f * lenSec / rt);
        gLow[(size_t) i] = std::pow (10.0f, -3.0f * lenSec / rtLow);
        hsGain[(size_t) i] = std::pow (10.0f, -3.0f * lenSec / rtHigh) / gHigh[(size_t) i];
        lenSm[(size_t) i].target = baseLen[(size_t) i];
        xover[(size_t) i].setCutoff (prm.bassXover, sr);
        hfDamp[(size_t) i].set (prm.highFreq, 0.7071f, sr);
        bwLoop[(size_t) i].setCutoff (loopBw, sr);
        lfoInc[(size_t) i] = prm.modRate * m.modRateMul * (0.8f + 0.45f * (float) i / (float) N) / sr;
    }

    hcL.set (prm.highCut, 0.707f, sr); hcR.set (prm.highCut, 0.707f, sr);
    lcL.set (prm.lowCut, 0.707f, sr);  lcR.set (prm.lowCut, 0.707f, sr);
    dirty = false;
}

void ReverbEngine::process (float* left, float* right, int numSamples)
{
    if (dirty)
        updateCoefficients();

    const ModeDef& m = getMode (prm.mode);
    const bool freeze = prm.freeze;
    const bool nonlin = m.nonlin;
    const float spread = m.spread;
    const float widthT = clampf (prm.width, 0.0f, 1.5f);
    const float dens = m.density;
    const float envA = std::exp (-1.0f / (0.05f * sr));
    const float drv = 1.0f + 2.5f * drive;

    for (int n = 0; n < numSamples; ++n)
    {
        float inL = left[n], inR = right[n];
        if (freeze) { inL = 0.0f; inR = 0.0f; }

        // pre-delay
        preL.push (bwL.process (inL));
        preR.push (bwR.process (inR));
        const float pd = std::max (2.0f, preSm.tick());
        float xL = preL.read (pd), xR = preR.read (pd);

        // input diffusion
        for (int i = 0; i < kInAP; ++i)
        {
            xL = apL[(size_t) i].process (xL);
            xR = apR[(size_t) i].process (xR);
        }

        // attack smear / early reflections
        erL.push (xL);
        erR.push (xR);
        float eL = 0.0f, eR = 0.0f;
        for (int k = 0; k < kErTaps; ++k)
        {
            eL += erL.tap ((int) erTimeL[(size_t) k]) * erGain[(size_t) k];
            eR += erR.tap ((int) erTimeR[(size_t) k]) * erGain[(size_t) k];
        }

        float oL = 0.0f, oR = 0.0f;

        if (nonlin)
        {
            for (int k = 0; k < kNlTaps; ++k)
            {
                const int d = (int) nlTime[(size_t) k];
                const float g = nlGain[(size_t) k];
                oL += erL.tap (d) * g * (k % 2 ? 0.8f : 1.0f);
                oR += erR.tap (d + (k % 3) * 7) * g * (k % 2 ? 1.0f : 0.8f);
            }
            oL *= 0.35f; oR *= 0.35f;
        }
        else
        {
            const float att = clampf (prm.attack, 0.0f, 1.0f);
            const float tL = xL * (1.0f - att) + eL * (0.4f + 0.6f * att);
            const float tR = xR * (1.0f - att) + eR * (0.4f + 0.6f * att);

            float y[N];
            for (int i = 0; i < N; ++i)
            {
                float mod = 0.0f;
                switch (m.mod)
                {
                    case ModType::Sine:
                    case ModType::Chorus:
                        lfoPhase[(size_t) i] += lfoInc[(size_t) i];
                        if (lfoPhase[(size_t) i] >= 1.0f) lfoPhase[(size_t) i] -= 1.0f;
                        mod = std::sin (lfoPhase[(size_t) i] * kTwoPi);
                        break;
                    case ModType::Random:
                        mod = rnd[(size_t) i].tick (lfoInc[(size_t) i] * 2.0f);
                        break;
                    case ModType::Chaotic:
                    {
                        float& w = walk[(size_t) i];
                        w += noise.next() * lfoInc[(size_t) i] * 60.0f - w * lfoInc[(size_t) i] * 2.0f;
                        w = clampf (w, -1.0f, 1.0f);
                        mod = w * 1.5f + 0.3f * rnd[(size_t) i].tick (lfoInc[(size_t) i] * 5.0f);
                        break;
                    }
                }
                const float len = lenSm[(size_t) i].tick();
                float v = line[(size_t) i].read (len + modSamples * 2.0f + 4.0f + mod * modSamples);
                v = lineAp[(size_t) i].process (v, mod * modSamples * 0.25f);

                if (! freeze)
                {
                    const float lo = xover[(size_t) i].process (v);
                    v = lo * gLow[(size_t) i] + (v - lo) * gHigh[(size_t) i];
                    float lp, bp, hp;
                    hfDamp[(size_t) i].tick (v, lp, bp, hp);
                    v += (hsGain[(size_t) i] - 1.0f) * hp;   // high shelf: only the band above HighFreq decays faster
                    if (loopBwOn) v = bwLoop[(size_t) i].process (v);
                    if (drive > 0.0f)
                        v = softClip (v * drv) / drv;
                }
                y[i] = v;
            }

            for (int i = 0; i < N; ++i)
            {
                const float s = 0.5f + 0.5f * spread;
                oL += y[i] * kSignL[i] * (i % 2 ? s : 1.0f);
                oR += y[i] * kSignR[i] * (i % 2 ? 1.0f : s);
            }
            const float tapMix = 0.25f * dens;
            oL = oL * 0.3f + tapMix * (line[1].tap ((int) (baseLen[1] * 0.37f) + 2) - line[6].tap ((int) (baseLen[6] * 0.61f) + 2));
            oR = oR * 0.3f + tapMix * (line[2].tap ((int) (baseLen[2] * 0.41f) + 2) - line[5].tap ((int) (baseLen[5] * 0.53f) + 2));

            hadamard8 (y);
            for (int i = 0; i < N; ++i)
            {
                const float inj = (i % 2 ? tR : tL) * (i < 4 ? 0.5f : -0.5f);
                line[(size_t) i].push (y[i] + (freeze ? 0.0f : inj));
            }
            oL += erOut * eL * 0.6f;
            oR += erOut * eR * 0.6f;
            if (chorusOut > 0.0f)
            {
                const float c = std::sin (lfoPhase[0] * kTwoPi) * modSamples;
                oL += chorusOut * 0.15f * (line[3].read (baseLen[3] * 0.5f + modSamples * 2.0f + 4.0f + c));
                oR += chorusOut * 0.15f * (line[4].read (baseLen[4] * 0.5f + modSamples * 2.0f + 4.0f - c));
            }
        }

        // make-up gain: ~unity wet energy at 2 s RT (measured by SITests)
        oL *= outGain;
        oR *= outGain;

        // a little L/R blend: VintageVerb tails are slightly correlated (+0.1..0.25), never anti-phase (better in mono)
        {
            const float l0 = oL, r0 = oR;
            oL = (l0 + 0.15f * r0) * 0.9889f;
            oR = (r0 + 0.15f * l0) * 0.9889f;
        }
        // width (M/S)
        widthSm += 0.001f * (widthT - widthSm);
        const float mid = 0.5f * (oL + oR), side = 0.5f * (oL - oR) * widthSm;
        oL = mid + side;
        oR = mid - side;

        // output EQ + colour
        oL = lcL.highpass (hcL.lowpass (oL));
        oR = lcR.highpass (hcR.lowpass (oR));
        if (quantStep > 0.0f)
        {
            oL = outBwL.process (std::round (oL / quantStep) * quantStep);
            oR = outBwR.process (std::round (oR / quantStep) * quantStep);
        }

        const float e = std::max (std::abs (oL), std::abs (oR));
        envOut = e > envOut ? e : e + envA * (envOut - e);

        left[n] = oL;
        right[n] = oR;
    }
}
} // namespace si::dsp
