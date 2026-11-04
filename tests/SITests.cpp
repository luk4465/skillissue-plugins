// SkillIssue test runner: DSP checks + a minimal VST3 host (load, process, state, editor layout lint).
// Usage: SITests [path/to/SI Reverb.vst3] [path/to/SI Riser.vst3]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "dsp/ReverbEngine.h"
#include "RiserRenderer.h"

using namespace juce;

static int failures = 0;
static void check (bool ok, const String& what)
{
    if (! ok) { ++failures; std::cout << "  FAIL  " << what << "\n"; }
}

static bool finite (const float* d, int n)
{
    for (int i = 0; i < n; ++i)
        if (! std::isfinite (d[i]) || std::abs (d[i]) > 100.0f) return false;
    return true;
}

// ---------------------------------------------------------------- reverb engine
/** Tail quality of the reverb: HF vs broadband decay, metallic ringing (spectral peaks) and stereo correlation. */
static void testReverbQuality()
{
    std::cout << "[reverb] tail quality (decay 2.5 s, size 60, HighShelf -6 dB @ 5 kHz)\n";
    constexpr double sr = 48000.0;
    auto rt60 = [=] (const std::vector<float>& x)
    {
        std::vector<double> edc (x.size());
        double acc = 0.0;
        for (int i = (int) x.size() - 1; i >= 0; --i) { acc += (double) x[(size_t) i] * x[(size_t) i]; edc[(size_t) i] = acc; }
        int t5 = -1, t25 = -1;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            const double db = 10.0 * std::log10 (edc[(size_t) i] / edc[0] + 1e-30);
            if (t5 < 0 && db < -5.0) t5 = i;
            if (t25 < 0 && db < -25.0) { t25 = i; break; }
        }
        return (t5 >= 0 && t25 > t5) ? (t25 - t5) / sr * 3.0 : -1.0;
    };
    for (int m : { 0, 2, 3, 11 })
    {
        si::dsp::ReverbEngine e;
        e.prepare (sr, 256);
        si::dsp::ReverbParams p;
        p.mode = m; p.color = 2; p.decay = 2.5f; p.size = 0.6f; p.preDelayMs = 0.0f; p.highCut = 20000.0f; p.lowCut = 20.0f;
        e.setParams (p);
        const int total = (int) (sr * 6.0);
        std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
        L[0] = R[0] = 1.0f;
        for (int i = 0; i < total; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, total - i));
        // high band (> ~6 kHz) decay
        std::vector<float> hi ((size_t) total), mid ((size_t) total);
        si::dsp::Svf h1, h2, b1;
        h1.set (6000.0f, 0.707f, (float) sr); h2.set (6000.0f, 0.707f, (float) sr); b1.set (1000.0f, 1.0f, (float) sr);
        for (int i = 0; i < total; ++i) { hi[(size_t) i] = h2.highpass (h1.highpass (L[(size_t) i])); mid[(size_t) i] = b1.bandpass (L[(size_t) i]); }
        const double rtAll = rt60 (L), rtHi = rt60 (hi), rtMid = rt60 (mid);
        // metallic ringing: FFT of the tail 0.25..0.6 s, peak above a 1/3-octave average (1-8 kHz)
        constexpr int order = 14, nfft = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) nfft * 2, 0.0f);
        const int start = (int) (sr * 0.25);
        for (int i = 0; i < nfft; ++i)
            buf[(size_t) i] = L[(size_t) (start + i)] * (0.5f - 0.5f * std::cos (6.2831853f * i / (nfft - 1)));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        double worst = 0.0;
        for (int k = (int) (1000.0 * nfft / sr); k < (int) (8000.0 * nfft / sr); ++k)
        {
            const int lo = (int) (k / 1.12), hiK = (int) (k * 1.12);
            double avg = 0.0;
            for (int j = lo; j <= hiK; ++j) avg += (double) buf[(size_t) j] * buf[(size_t) j];
            avg /= (hiK - lo + 1);
            worst = std::max (worst, 10.0 * std::log10 ((double) buf[(size_t) k] * buf[(size_t) k] / (avg + 1e-30)));
        }
        // stereo correlation of the tail
        double lr = 0, ll = 0, rr = 0;
        for (int i = (int) (sr * 0.1); i < (int) (sr * 1.0); ++i) { lr += L[(size_t) i] * R[(size_t) i]; ll += L[(size_t) i] * L[(size_t) i]; rr += R[(size_t) i] * R[(size_t) i]; }
        const double corr = lr / std::sqrt (ll * rr + 1e-30);
        std::cout << "  " << String (si::dsp::getMode (m).name).paddedRight (' ', 14) << " RT " << String (rtAll, 2) << " s, 1k " << String (rtMid, 2)
                  << " s, >6k " << String (rtHi, 2) << " s, ringing peak +" << String (worst, 1) << " dB, L/R corr " << String (corr, 2) << "\n";
        check (rtHi > 0.35 * rtMid, String (si::dsp::getMode (m).name) + ": highs die too fast");
        check (std::abs (corr) < 0.5, String (si::dsp::getMode (m).name) + ": tail too mono");
    }
}

static void testReverbModes()
{
    std::cout << "[reverb] 22 modes x 3 colors, impulse response\n";
    constexpr double sr = 48000.0;
    for (int m = 0; m < si::dsp::kNumModes; ++m)
    {
        String line;
        for (int c = 0; c < 3; ++c)
        {
            si::dsp::ReverbEngine e;
            e.prepare (sr, 256);
            si::dsp::ReverbParams p;
            p.mode = m; p.color = c; p.decay = 2.0f; p.size = 0.6f; p.preDelayMs = 0.0f;
            e.setParams (p);
            const int total = (int) (sr * 5.0);
            std::vector<float> L ((size_t) total, 0.0f), R ((size_t) total, 0.0f);
            L[0] = R[0] = 1.0f;
            for (int i = 0; i < total; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, total - i));
            const bool ok = finite (L.data(), total) && finite (R.data(), total);
            check (ok, String ("mode ") + si::dsp::getMode (m).name + " color " + String (c) + " not finite");

            // Schroeder backward integration -> T20 extrapolated RT60
            std::vector<double> edc ((size_t) total);
            double acc = 0.0;
            for (int i = total - 1; i >= 0; --i) { acc += (double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]; edc[(size_t) i] = acc; }
            int t5 = -1, t25 = -1;
            for (int i = 0; i < total; ++i)
            {
                const double db = 10.0 * std::log10 (edc[(size_t) i] / edc[0] + 1e-30);
                if (t5 < 0 && db < -5.0) t5 = i;
                if (t25 < 0 && db < -25.0) { t25 = i; break; }
            }
            const double rt = (t5 >= 0 && t25 > t5) ? (t25 - t5) / sr * 3.0 : -1.0;
            float pk = 0.0f;
            for (int i = 0; i < total; ++i) pk = std::max (pk, std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])));
            double energy = 0.0;
            for (int i = 0; i < total; ++i) energy += 0.5 * ((double) L[(size_t) i] * L[(size_t) i] + (double) R[(size_t) i] * R[(size_t) i]);
            const double edb = 10.0 * std::log10 (energy + 1e-30);
            line << "  c" << c << " RT~" << String (rt, 2) << "s E " << String (edb, 1) << "dB";
            if (! si::dsp::getMode (m).nonlin)
                check (rt > 0.6 && rt < 6.0, String ("mode ") + si::dsp::getMode (m).name + " RT60 implausible: " + String (rt, 2));
            check (edb > -9.0 && edb < 6.0 && pk < 2.0f, String ("mode ") + si::dsp::getMode (m).name + " energy " + String (edb, 1) + " dB");
        }
        std::cout << "  " << String (si::dsp::getMode (m).name).paddedRight (' ', 16) << line << "\n";
    }

    // long decay + freeze stability
    {
        si::dsp::ReverbEngine e;
        e.prepare (sr, 512);
        si::dsp::ReverbParams p;
        p.mode = 9; p.decay = 70.0f; p.size = 1.0f;
        e.setParams (p);
        Random rnd (1);
        std::vector<float> L (512), R (512);
        float maxOut = 0.0f;
        for (int b = 0; b < (int) (sr * 20 / 512); ++b)
        {
            for (int i = 0; i < 512; ++i) { L[(size_t) i] = b < 40 ? rnd.nextFloat() - 0.5f : 0.0f; R[(size_t) i] = L[(size_t) i]; }
            if (b == 100) { p.freeze = true; e.setParams (p); }
            e.process (L.data(), R.data(), 512);
            check (finite (L.data(), 512), "70 s decay / freeze not finite");
            for (auto v : L) maxOut = std::max (maxOut, std::abs (v));
        }
        check (maxOut < 3.0f, "70 s decay / freeze grows: " + String (maxOut));
        std::cout << "[reverb] 70 s decay + freeze, 20 s: max " << maxOut << "\n";
    }
}

// ---------------------------------------------------------------- riser renderer
static AudioBuffer<float> fakeVocal (double sr)
{
    AudioBuffer<float> b (2, (int) (sr * 1.6));
    b.clear();
    Random rnd (7);
    const int start = (int) (0.3 * sr);
    for (int i = 0; i < (int) (0.7 * sr); ++i)          // "ah" with vibrato
    {
        const double t = i / sr;
        const float env = (float) std::min (1.0, t * 30.0) * (float) std::min (1.0, (0.7 - t) * 20.0);
        float v = 0.0f;
        for (int h = 1; h <= 8; ++h) v += (float) std::sin (2.0 * MathConstants<double>::pi * 220.0 * h * t + 0.3 * std::sin (t * 30.0)) / (float) h;
        b.setSample (0, start + i, 0.3f * env * v);
        b.setSample (1, start + i, 0.3f * env * v);
    }
    for (int i = 0; i < (int) (0.15 * sr); ++i)          // "s"
    {
        const float s = 0.25f * (rnd.nextFloat() - 0.5f);
        b.setSample (0, start + (int) (0.75 * sr) + i, s);
        b.setSample (1, start + (int) (0.75 * sr) + i, s);
    }
    return b;
}

static void testRiser()
{
    std::cout << "[riser] render\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        auto src = fakeVocal (sr);
        si::RiserSettings s;
        s.bpm = 120.0; s.lengthBeats = 4.0;
        const auto t0 = Time::getMillisecondCounterHiRes();
        auto r = si::renderRiser (s, src, sr);
        const auto ms = Time::getMillisecondCounterHiRes() - t0;
        check (r != nullptr, "riser null at " + String (sr));
        if (r == nullptr) continue;
        const int expected = (int) std::round (2.0 * sr);
        check (r->riserSamples == expected, "riser length " + String (r->riserSamples) + " != " + String (expected));
        const auto& a = r->audio;
        check (finite (a.getReadPointer (0), a.getNumSamples()) && finite (a.getReadPointer (1), a.getNumSamples()), "riser not finite");
        const int L = r->riserSamples;
        const float pk = std::max (a.getMagnitude (0, 0, L), a.getMagnitude (1, 0, L));
        const float rmsA = a.getRMSLevel (0, 0, L / 4), rmsB = a.getRMSLevel (0, L * 3 / 4, L / 4);
        const float target = jlimit (0.1f, 0.891f, std::max (src.getMagnitude (0, 0, src.getNumSamples()), src.getMagnitude (1, 0, src.getNumSamples())) * 0.891f);
        check (std::abs (pk - target) < 0.01f, "normalize peak " + String (pk) + " expected " + String (target));
        check (rmsB > rmsA * 2.0f, "riser does not swell (first " + String (rmsA) + " last " + String (rmsB) + ")");
        std::cout << "  sr " << sr << ": " << String (ms, 0) << " ms, len " << a.getNumSamples() << ", swell x" << String (rmsB / jmax (1e-6f, rmsA), 1) << "\n";
    }

    // every FX + extremes, all modes
    auto src = fakeVocal (48000.0);
    for (int m = 0; m < si::dsp::kNumModes; ++m)
    {
        si::RiserSettings s;
        s.verbMode = m; s.pitch = -24.0f; s.filter = 0.8f; s.filterType = m % 3;
        s.phone = s.chorus = s.tremolo = s.stutter = s.lofi = s.echo = (m % 2 == 0);
        s.lengthBeats = (m % 3 == 0) ? 32.0 : 1.0;
        s.feed = m % 7;
        s.includeDry = m % 2;
        auto r = si::renderRiser (s, src, 48000.0);
        check (r != nullptr && finite (r->audio.getReadPointer (0), r->audio.getNumSamples()), "riser FX mode " + String (m));
    }
    AudioBuffer<float> silence (2, 48000);
    silence.clear();
    check (si::renderRiser ({}, silence, 48000.0) == nullptr, "silence should give no riser");
}

// ---------------------------------------------------------------- EQ
#if SI_TEST_EQ
#include "EqEngine.h"
static float sineGainDb (si::eq::EqEngine& e, float f, float amp, double sr)
{
    e.reset();
    const int n = (int) (sr * 0.5);
    std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = amp * (float) std::sin (2.0 * MathConstants<double>::pi * f * i / sr);
    for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
    double acc = 0.0;
    for (int i = n / 2; i < n; ++i) acc += (double) L[(size_t) i] * L[(size_t) i];
    const float rms = (float) std::sqrt (acc / (n - n / 2));
    return Decibels::gainToDecibels (rms / (amp * 0.70710678f));
}

static void testEq()
{
    using namespace si::eq;
    std::cout << "[eq] measured vs analytic response\n";
    const double sr = 48000.0;
    struct Case { BandParams p; float f; };
    std::vector<Case> cases;
    auto mk = [] (Type t, float f, float g, float q, int slope = 1) { BandParams p; p.on = true; p.type = t; p.freq = f; p.gain = g; p.q = q; p.slope = slope; return p; };
    cases.push_back ({ mk (Type::Bell, 1000, 6, 1.0f), 1000 });
    cases.push_back ({ mk (Type::Bell, 1000, -9, 2.0f), 1000 });
    cases.push_back ({ mk (Type::LowShelf, 100, 4, 0.71f), 30 });
    cases.push_back ({ mk (Type::HighShelf, 8000, -6, 0.71f), 16000 });
    cases.push_back ({ mk (Type::LowCut, 100, 0, 0.71f, 1), 50 });
    cases.push_back ({ mk (Type::LowCut, 100, 0, 0.71f, 3), 50 });
    cases.push_back ({ mk (Type::HighCut, 5000, 0, 0.71f, 0), 10000 });
    cases.push_back ({ mk (Type::Notch, 1000, 0, 4.0f), 1100 });
    cases.push_back ({ mk (Type::Tilt, 1000, 6, 0.5f), 100 });
    for (auto& c : cases)
    {
        EqEngine e;
        e.prepare (sr);
        e.setBand (0, c.p);
        const float meas = sineGainDb (e, c.f, 0.25f, sr);
        const float ana = bandResponseDb (c.p, c.p.gain, c.f, (float) sr);
        std::cout << "  type " << (int) c.p.type << " @" << c.f << " Hz: measured " << String (meas, 2) << " dB, curve " << String (ana, 2) << " dB\n";
        check (std::abs (meas - ana) < 0.35f, "eq curve mismatch type " + String ((int) c.p.type));
    }
    check (std::abs (bandResponseDb (mk (Type::LowCut, 100, 0, 0.71f, 1), 0, 50, 48000) + 24.0f) < 1.0f, "24 dB/oct slope");

    // dynamic band: loud signal pulls the gain towards the target, quiet does not
    BandParams d = mk (Type::Bell, 1000, 0, 1.0f);
    d.dyn = true; d.thresh = -30.0f; d.target = -10.0f;
    EqEngine e;
    e.prepare (sr);
    e.setBand (0, d);
    const float loud = sineGainDb (e, 1000, 0.5f, sr);
    const float quiet = sineGainDb (e, 1000, 0.003f, sr);
    std::cout << "  dynamic: loud " << String (loud, 2) << " dB, quiet " << String (quiet, 2) << " dB\n";
    check (loud < -8.0f && quiet > -1.0f, "dynamic eq behaviour");
}
#endif

// ---------------------------------------------------------------- Tune
#if SI_TEST_TUNE
#include "TuneEngine.h"
static float measureHz (const std::vector<float>& x, int from, int len, double sr)
{
    // YIN-style: first lag whose normalised difference dips below 0.1 (avoids octave errors)
    int best = 0;
    double vals[2048] {}, run = 0.0;
    const int lo = (int) (sr / 1000), hi = (int) (sr / 60);
    for (int lag = 1; lag <= hi; ++lag)
    {
        double acc = 0.0;
        for (int i = 0; i < len; ++i) { const double d = x[(size_t) (from + i)] - x[(size_t) (from + i + lag)]; acc += d * d; }
        run += acc;
        vals[lag] = run > 0 ? acc * lag / run : 1.0;
    }
    for (int lag = lo; lag < hi; ++lag)
        if (vals[lag] < 0.1) { while (lag + 1 < hi && vals[lag + 1] < vals[lag]) ++lag; best = lag; break; }
    if (best == 0) return 0.0f;
    double t = best;
    if (best > lo && best < hi)
    {
        const double a = vals[best - 1], b = vals[best], c = vals[best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) t += 0.5 * (a - c) / den;
    }
    return (float) (sr / t);
}

static void testTune()
{
    using namespace si::tune;
    std::cout << "[tune] pitch correction\n";
    const double sr = 48000.0;
    struct Case { float inHz; int mask; float transpose; float expect; const char* name; };
    const int cMajor = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11);
    const int bbMinor = (1 << 10) | (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8);
    const Case cases[] = { { 450.0f, 0xFFF, 0.0f, 440.0f, "450 Hz -> A4" }, { 220.0f, 0xFFF, 12.0f, 440.0f, "A3 +12 st" },
                           { 270.0f, cMajor, 0.0f, 261.63f, "270 Hz in C major -> C4" }, { 155.0f, 0xFFF, 0.0f, 155.56f, "155 Hz -> D#3" },
                           { 488.0f, bbMinor, 0.0f, 466.16f, "488 Hz in Bb minor -> Bb4" }, { 335.0f, bbMinor, 0.0f, 349.23f, "335 Hz in Bb minor -> F4" } };
    for (const auto& c : cases)
    {
        for (bool low : { true, false })
        {
            TuneEngine e;
            TuneParams p;
            p.scaleMask = c.mask; p.speedMs = 0.0f; p.amount = 1.0f; p.transpose = c.transpose; p.lowLatency = low; p.stabilizer = 0;
            e.setParams (p);
            e.prepare (sr);
            const int n = (int) (sr * 1.5);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                double v = 0.0;
                for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * MathConstants<double>::pi * c.inHz * h * i / sr) / h;
                L[(size_t) i] = R[(size_t) i] = (float) (0.25 * v);
            }
            for (int i = 0; i < n; i += 128) e.process (L.data() + i, R.data() + i, std::min (128, n - i));
            const bool fin = finite (L.data(), n);
            const float hz = measureHz (L, n - 6000, 2048, sr);
            const float cents = 1200.0f * std::log2 (hz / c.expect);
            std::cout << "  " << String (c.name).paddedRight (' ', 26) << (low ? "low-lat " : "quality ") << String (hz, 2) << " Hz (" << String (cents, 1)
                      << " ct), latency " << String (e.latencySamples() / sr * 1000.0, 2) << " ms\n";
            check (fin && std::abs (cents) < 8.0f, String ("tune case ") + c.name);
        }
    }
}
#endif

// ---------------------------------------------------------------- Saturn
#if SI_TEST_SAT
#include "SaturnEngine.h"
static void testSaturn()
{
    using namespace si::sat;
    std::cout << "[saturn] crossover flatness, styles, auto gain\n";
    const double sr = 96000.0;   // runs oversampled in the plugin
    for (int cfg = 0; cfg < 5 * 4; ++cfg)
    {
        const int nb = 2 + cfg % 5, slope = cfg / 5;
        float worst = 0.0f;
        for (float f : { 40.f, 150.f, 400.f, 1200.f, 3000.f, 7000.f, 15000.f })
        {
            SaturnEngine e;
            e.prepare (sr);
            Settings s;
            s.bands = nb;
            s.autoGain = false;
            s.slope.fill (slope);
            for (auto& b : s.band) b.mix = 0.0f;   // dry through the splitter only
            e.setSettings (s);
            const int n = (int) (sr * 0.3);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.25f * (float) std::sin (2.0 * MathConstants<double>::pi * f * i / sr);
            for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
            double acc = 0;
            for (int i = n / 2; i < n; ++i) acc += (double) L[(size_t) i] * L[(size_t) i];
            const float db = Decibels::gainToDecibels ((float) std::sqrt (acc / (n - n / 2)) / (0.25f * 0.70710678f));
            worst = std::max (worst, std::abs (db));
        }
        std::cout << "  " << nb << " bands: max deviation " << String (worst, 3) << " dB\n";
        check (worst < 0.1f, "saturn crossover not flat with " + String (nb) + " bands");
    }
    for (int st = 0; st < kNumStyles; ++st)
    {
        SaturnEngine e;
        e.prepare (sr);
        Settings s;
        s.bands = 3;
        for (auto& b : s.band) { b.style = st; b.driveDb = 36.0f; b.dyn = 0.0f; b.treble = 6.0f; b.feedback = 0.6f; }
        e.setSettings (s);
        const int n = (int) (sr * 0.3);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        Random rnd (st);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.5f * (rnd.nextFloat() - 0.5f) + 0.3f * (float) std::sin (i * 0.01);
        double inAcc = 0;
        for (auto v : L) inAcc += (double) v * v;
        for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
        double outAcc = 0;
        for (int i = n / 2; i < n; ++i) outAcc += (double) L[(size_t) i] * L[(size_t) i];
        const float diff = Decibels::gainToDecibels ((float) std::sqrt (outAcc / (n - n / 2)) / (float) std::sqrt (inAcc / n));
        check (finite (L.data(), n), String ("saturn style not finite: ") + styleName (st));
        check (std::abs (diff) < 6.0f, String ("saturn auto gain off by ") + String (diff, 1) + " dB: " + styleName (st));
    }
}
#endif

// ---------------------------------------------------------------- Echo + Rack
#if SI_TEST_ECHO
#include "EchoEngine.h"
#include "Modules.h"
static void testEchoRack()
{
    using namespace si::echo;
    std::cout << "[echo] timing, stability\n";
    const double sr = 48000.0;
    {
        EchoEngine e;
        EchoParams p; p.mode = Mode::Single; p.style = 0; p.timeL = p.timeR = 0.25; p.feedback = 0.5f; p.mix = 1.0f; p.lowCut = 20; p.highCut = 20000; p.drive = 0;
        e.setParams (p);
        e.prepare (sr);
        const int n = (int) sr;
        std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
        L[0] = R[0] = 0.5f;
        for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
        int peak = 1; float pv = 0;
        for (int i = 100; i < (int) (sr * 0.4); ++i) if (std::abs (L[(size_t) i]) > pv) { pv = std::abs (L[(size_t) i]); peak = i; }
        std::cout << "  first echo at " << peak << " samples (expected " << (int) (0.25 * sr) << ")\n";
        check (std::abs (peak - (int) (0.25 * sr)) <= 3, "echo time");
    }
    {
        // stereo image: a centred (mono) vocal must give centred echoes in Single and a balanced bounce in Ping-Pong
        auto wetEnergy = [sr] (Mode m, float fb, double& eL, double& eR)
        {
            EchoEngine e;
            EchoParams p; p.mode = m; p.style = 0; p.timeL = p.timeR = 0.1; p.feedback = fb; p.mix = 1.0f; p.lowCut = 20; p.highCut = 20000; p.drive = 0;
            e.setParams (p);
            e.prepare (sr);
            const int n = (int) sr * 2;
            std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
            Random rnd (7);
            for (int i = 0; i < 2400; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f * (rnd.nextFloat() - 0.5f);
            for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
            eL = eR = 0.0;
            for (int i = 0; i < n; ++i) { eL += L[(size_t) i] * L[(size_t) i]; eR += R[(size_t) i] * R[(size_t) i]; }
        };
        double eL, eR;
        wetEnergy (Mode::Single, 0.35f, eL, eR);
        const double single = 10.0 * std::log10 (eL / eR);
        wetEnergy (Mode::PingPong, 0.35f, eL, eR);
        const double ping35 = 10.0 * std::log10 (eL / eR);
        wetEnergy (Mode::PingPong, 0.7f, eL, eR);
        const double ping70 = 10.0 * std::log10 (eL / eR);
        std::cout << "  L/R balance: single " << String (single, 2) << " dB, ping-pong fb35 " << String (ping35, 2) << " dB, fb70 " << String (ping70, 2) << " dB\n";
        check (std::abs (single) < 0.1, "single echo centred");
        check (std::abs (ping35) < 2.5 && std::abs (ping70) < 2.5, "ping-pong balanced");
    }
    {
        // HiPass / LoPass act on the very first echo (12 dB/oct)
        auto firstEcho = [sr] (float hz, float lowCut, float highCut)
        {
            EchoEngine e;
            EchoParams p; p.mode = Mode::Single; p.style = 0; p.timeL = p.timeR = 0.05; p.feedback = 0.0f; p.mix = 1.0f; p.lowCut = lowCut; p.highCut = highCut; p.drive = 0;
            e.setParams (p);
            e.prepare (sr);
            const int n = (int) (sr * 0.5);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.1f * (float) std::sin (2.0 * 3.14159265358979 * hz * i / sr);
            for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
            double s = 0;
            for (int i = n / 2; i < n; ++i) s += L[(size_t) i] * L[(size_t) i];
            return 10.0 * std::log10 (s / (n / 2) + 1e-20);
        };
        const double hp = firstEcho (100.0f, 1000.0f, 20000.0f) - firstEcho (100.0f, 20.0f, 20000.0f);
        const double lp = firstEcho (8000.0f, 20.0f, 1000.0f) - firstEcho (8000.0f, 20.0f, 20000.0f);
        std::cout << "  HiPass 1 kHz on 100 Hz: " << String (hp, 1) << " dB, LoPass 1 kHz on 8 kHz: " << String (lp, 1) << " dB\n";
        check (hp < -30.0 && lp < -30.0, "echo filters");
    }
    {
        // a loud vocal (-6 dBFS) must come back clean at Saturation 0: level kept, 3rd harmonic far down
        auto echoOf = [sr] (int style, float drive, double& levelDb, double& h3Db)
        {
            EchoEngine e;
            EchoParams p; p.mode = Mode::Single; p.style = style; p.timeL = p.timeR = 0.05; p.feedback = 0.0f; p.mix = 1.0f; p.lowCut = 20; p.highCut = 20000; p.drive = drive;
            e.setParams (p);
            e.prepare (sr);
            const int n = (int) sr;
            const double f0 = 750.0;
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.14159265358979 * f0 * i / sr);
            for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
            auto goertzel = [&] (double f)
            {
                double s1 = 0, s2 = 0;
                const double c = 2.0 * std::cos (2.0 * 3.14159265358979 * f / sr);
                for (int i = n / 2; i < n; ++i) { const double s0 = L[(size_t) i] + c * s1 - s2; s2 = s1; s1 = s0; }
                return std::sqrt (s1 * s1 + s2 * s2 - c * s1 * s2) / (n / 4);
            };
            const double a1 = goertzel (f0), a3 = goertzel (3.0 * f0);
            levelDb = 20.0 * std::log10 (a1 / 0.5);
            h3Db = 20.0 * std::log10 (a3 / a1 + 1e-12);
        };
        double lvClean, h3Clean, lvTape, h3Tape, lvDriven, h3Driven;
        echoOf (0, 0.0f, lvClean, h3Clean);
        echoOf (1, 0.0f, lvTape, h3Tape);
        echoOf (1, 1.0f, lvDriven, h3Driven);
        std::cout << "  -6 dBFS echo: clean " << String (lvClean, 2) << " dB / H3 " << String (h3Clean, 1) << " dB, studio tape " << String (lvTape, 2)
                  << " dB / H3 " << String (h3Tape, 1) << " dB, saturation 100% H3 " << String (h3Driven, 1) << " dB\n";
        check (std::abs (lvClean) < 0.5 && h3Clean < -40.0, "clean echo is clean");
        check (std::abs (lvTape) < 1.0 && h3Tape < -30.0, "studio tape echo is not squashed");
        check (h3Driven > -25.0, "saturation knob still drives");
    }
    for (int m = 0; m < 4; ++m)
        for (int st = 0; st < kNumStyles; ++st)
        {
            EchoEngine e;
            EchoParams p; p.mode = (Mode) m; p.style = st; p.timeL = 0.12; p.timeR = 0.2; p.feedback = 1.1f; p.mix = 0.5f; p.diffusion = 0.5f; p.wobble = 0.5f; p.ducking = 0.5f;
            e.setParams (p);
            e.prepare (sr);
            const int n = (int) (sr * 3);
            std::vector<float> L ((size_t) n), R ((size_t) n);
            Random rnd (st);
            for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = i < 4800 ? rnd.nextFloat() - 0.5f : 0.0f;
            for (int i = 0; i < n; i += 256) e.process (L.data() + i, R.data() + i, std::min (256, n - i));
            float mx = 0;
            for (int i = 0; i < n; ++i) mx = std::max (mx, std::abs (L[(size_t) i]));
            check (finite (L.data(), n) && mx < 4.0f, "echo mode " + String (m) + " style " + styleName (st) + " max " + String (mx));
        }

    std::cout << "[rack] 13 modules, random settings\n";
    using namespace si::rack;
    for (int t = 1; t < kNumModules; ++t)
        for (int trial = 0; trial < 6; ++trial)
        {
            auto p = createProcessor (t);
            p->prepare (sr);
            Random rnd (t * 31 + trial);
            std::array<float, kParams> k;
            for (auto& v : k) v = rnd.nextFloat();
            const int n = 48000;
            std::vector<float> L ((size_t) n), R ((size_t) n);
            for (int i = 0; i < n; ++i) { L[(size_t) i] = 0.7f * (rnd.nextFloat() - 0.5f) * (float) std::sin (i * 0.002); R[(size_t) i] = L[(size_t) i] * 0.8f; }
            for (int i = 0; i < n; i += 512) p->process (L.data() + i, R.data() + i, std::min (512, n - i), k, trial % 4);
            check (finite (L.data(), n) && finite (R.data(), n), String ("rack module not finite: ") + moduleDef (t).name);
        }
    // a neutral EQ 73 / P1A must stay close to unity
    for (int t : { (int) EQ73, (int) EQP1A })
    {
        auto p = createProcessor (t);
        p->prepare (sr);
        std::array<float, kParams> k;
        for (int i = 0; i < kParams; ++i) k[(size_t) i] = knobDefaultNorm (t, i);
        const int n = 24000;
        std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.1f * (float) std::sin (2.0 * MathConstants<double>::pi * 1000.0 * i / sr);
        p->process (L.data(), R.data(), n, k, 0);
        float pk = 0;
        for (int i = n / 2; i < n; ++i) pk = std::max (pk, std::abs (L[(size_t) i]));
        check (std::abs (Decibels::gainToDecibels (pk / 0.1f)) < 0.5f, String ("neutral ") + moduleDef (t).name + " not flat: " + String (Decibels::gainToDecibels (pk / 0.1f), 2) + " dB");
    }
}
#endif

#if SI_TEST_TUNE
// natural settings must keep part of the singer's vibrato; hard tune must flatten it
static void testTuneNatural()
{
    using namespace si::tune;
    std::cout << "[tune] vibrato kept by Flex/Humanize\n";
    const double sr = 48000.0;
    auto depth = [&] (float speed, float flex, float hum)
    {
        TuneEngine e;
        TuneParams p;
        p.scaleMask = 0xFFF; p.speedMs = speed; p.flex = flex; p.humanize = hum; p.stabilizer = 0; p.lowLatency = true;
        e.setParams (p);
        e.prepare (sr);
        const int n = (int) (sr * 1.6);
        std::vector<float> L ((size_t) n), R ((size_t) n);
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double cents = 35.0 * std::sin (2.0 * MathConstants<double>::pi * 5.5 * i / sr);
            ph += 2.0 * MathConstants<double>::pi * 440.0 * std::pow (2.0, cents / 1200.0) / sr;
            double v = 0.0;
            for (int h = 1; h <= 6; ++h) v += std::sin (ph * h) / h;
            L[(size_t) i] = R[(size_t) i] = (float) (0.25 * v);
        }
        for (int i = 0; i < n; i += 128) e.process (L.data() + i, R.data() + i, std::min (128, n - i));
        float lo = 1e9f, hi = -1e9f;
        for (int k = 0; k < 40; ++k)
        {
            const int from = (int) (sr * 0.8) + k * 400;
            const float c = 1200.0f * std::log2 (measureHz (L, from, 600, sr) / 440.0f);
            lo = std::min (lo, c); hi = std::max (hi, c);
        }
        return 0.5f * (hi - lo);
    };
    const float hard = depth (0.0f, 0.0f, 0.0f), natural = depth (10.0f, 0.04f, 0.1f), loose = depth (25.0f, 0.3f, 0.3f);
    std::cout << "  vibrato depth (input 35 ct): hard " << String (hard, 1) << " ct, AT-like 10/4/10 " << String (natural, 1) << " ct, loose " << String (loose, 1) << " ct\n";
    check (hard < 10.0f, "hard tune should flatten vibrato");
    check (natural > hard + 5.0f && loose > natural, "Flex/Humanize should keep more of the vibrato");
}

// every key, major + minor, Auto-Tune-like settings: off-scale pitches must land on the nearest scale note
static void testTuneAllKeys()
{
    using namespace si::tune;
    std::cout << "[tune] all 24 keys (speed 10, flex 4, humanize 10)\n";
    const double sr = 48000.0;
    const int major[] = { 0, 2, 4, 5, 7, 9, 11 }, minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int fails = 0;
    float worst = 0.0f;
    for (int key = 0; key < 12; ++key)
        for (int sc = 0; sc < 2; ++sc)
        {
            int mask = 0;
            for (int iv : (sc == 0 ? major : minor)) mask |= 1 << ((key + iv) % 12);
            for (float off : { 0.62f, 3.4f, 5.55f, 8.3f, 10.7f })
            {
                const float midi = 52.0f + (float) key + off;          // somewhere between two notes
                // expected: nearest allowed note
                int best = 0; float bestD = 99.0f;
                for (int c = (int) midi - 7; c <= (int) midi + 7; ++c)
                    if ((mask >> (((c % 12) + 12) % 12)) & 1)
                        if (std::abs ((float) c - midi) < bestD) { bestD = std::abs ((float) c - midi); best = c; }
                if (std::abs (bestD - 0.5f) < 0.06f) continue;          // skip near-ties
                const float inHz = 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
                const float expHz = 440.0f * std::pow (2.0f, ((float) best - 69.0f) / 12.0f);
                TuneEngine e;
                TuneParams p;
                p.scaleMask = mask; p.speedMs = 10.0f; p.amount = 1.0f; p.humanize = 0.1f; p.flex = 0.04f; p.stabilizer = 0; p.lowLatency = true;
                e.setParams (p);
                e.prepare (sr);
                const int n = (int) (sr * 1.2);
                std::vector<float> L ((size_t) n), R ((size_t) n);
                for (int i = 0; i < n; ++i)
                {
                    double v = 0.0;
                    for (int h = 1; h <= 6; ++h) v += std::sin (2.0 * MathConstants<double>::pi * inHz * h * i / sr) / h;
                    L[(size_t) i] = R[(size_t) i] = (float) (0.25 * v);
                }
                for (int i = 0; i < n; i += 128) e.process (L.data() + i, R.data() + i, std::min (128, n - i));
                const float hz = measureHz (L, n - 6000, 2048, sr);
                const float cents = 1200.0f * std::log2 (hz / expHz);
                worst = std::max (worst, std::abs (cents));
                if (std::abs (cents) > 10.0f)
                {
                    ++fails;
                    std::cout << "    " << names[key] << (sc ? " minor" : " major") << ": in " << String (inHz, 1) << " Hz -> " << String (hz, 1)
                              << " Hz, expected " << String (expHz, 1) << " (" << String (cents, 1) << " ct)\n";
                }
            }
        }
    std::cout << "  worst error " << String (worst, 1) << " cents\n";
    check (fails == 0, "tuning wrong in " + String (fails) + " cases");
}
#endif

// ---------------------------------------------------------------- VST3 host
/** What the listener hears from the real SI Echo VST3: L / R level per 125 ms after a short vocal-like burst. */
static void echoListen (VST3PluginFormat& fmt, const PluginDescription& d, float modeNorm, const char* label)
{
    const double sr = 48000.0;
    const int bs = 480;
    String err;
    auto inst = fmt.createInstanceFromDescription (d, sr, bs, err);
    if (inst == nullptr) return;
    inst->setPlayConfigDetails (2, 2, sr, bs);
    inst->prepareToPlay (sr, bs);
    for (auto* p : inst->getParameters())
    {
        const auto n = p->getName (32);
        if (n == "Mode") p->setValue (modeNorm);
        if (n == "Mix") p->setValue (0.3f);
        if (n == "Feedback") p->setValue (45.0f / 110.0f);
    }
    AudioBuffer<float> buf (2, bs);
    MidiBuffer midi;
    Random rnd (5);
    const int win = (int) (sr * 0.125);
    double eL = 0, eR = 0;
    int cnt = 0;
    String row;
    for (int b = 0; b < (int) (sr * 2.5) / bs; ++b)
    {
        for (int i = 0; i < bs; ++i)
        {
            const float x = (b * bs + i) < (int) (sr * 0.1) ? 0.3f * (rnd.nextFloat() - 0.5f) : 0.0f;
            buf.setSample (0, i, x);
            buf.setSample (1, i, x);
        }
        inst->processBlock (buf, midi);
        for (int i = 0; i < bs; ++i)
        {
            eL += buf.getSample (0, i) * buf.getSample (0, i);
            eR += buf.getSample (1, i) * buf.getSample (1, i);
            if (++cnt == win)
            {
                auto db = [win] (double e) { return roundToInt (10.0 * std::log10 (e / win + 1e-12)); };
                row << " " << String (db (eL)) << "/" << String (db (eR));
                eL = eR = 0;
                cnt = 0;
            }
        }
    }
    std::cout << "  " << label << " L/R dB per 125 ms:" << row << "\n";
}

/** External sidechain through the real VST3 (the host enables the aux bus like Ableton does):
    a quiet main signal, a loud key; with SC on the effect must react to the key, with SC off ignore it. */
static void sidechainTest (VST3PluginFormat& fmt, const PluginDescription& d, const String& file)
{
    struct Setup { const char* plugin; std::vector<std::pair<const char*, float>> params; };   // param name -> normalised value
    std::vector<Setup> setups {
        { "SI 76",     { { "Input", 0.45f }, { "Mix", 1.0f } } },
        { "SI Echo",   { { "Ducking", 1.0f }, { "Mix", 1.0f }, { "Feedback", 0.5f } } },
        { "SI Reverb", { { "Duck", 1.0f }, { "Mix", 1.0f } } },
    };
    const Setup* su = nullptr;
    for (auto& s : setups) if (file.contains (s.plugin)) su = &s;
    if (su == nullptr) return;
    auto run = [&] (bool scOn, bool keyLoud)
    {
        const double sr = 48000.0;
        const int bs = 512;
        String err;
        auto inst = fmt.createInstanceFromDescription (d, sr, bs, err);
        if (inst == nullptr) return -1000.0;
        auto layout = inst->getBusesLayout();
        if (layout.inputBuses.size() < 2) return -2000.0;
        layout.inputBuses.getReference (1) = AudioChannelSet::stereo();
        if (! inst->setBusesLayout (layout)) return -3000.0;
        inst->prepareToPlay (sr, bs);
        for (auto* p : inst->getParameters())
        {
            const auto nm = p->getName (64);
            if (nm == "External Sidechain") p->setValue (scOn ? 1.0f : 0.0f);
            for (auto& [pn, v] : su->params) if (nm == pn) p->setValue (v);
        }
        const int chans = inst->getTotalNumInputChannels();
        AudioBuffer<float> buf (jmax (4, chans), bs);
        MidiBuffer midi;
        Random rnd (9);
        double e = 0.0;
        int cnt = 0;
        for (int b = 0; b < (int) (sr * 3 / bs); ++b)
        {
            for (int i = 0; i < bs; ++i)
            {
                const float t = (float) (b * bs + i) / (float) sr;
                const float mainSig = 0.03f * std::sin (6.2831853f * 220.0f * t);
                const float keySig = keyLoud ? 0.9f * (rnd.nextFloat() - 0.5f) : 0.0f;
                buf.setSample (0, i, mainSig); buf.setSample (1, i, mainSig);
                buf.setSample (2, i, keySig);  buf.setSample (3, i, keySig);
            }
            inst->processBlock (buf, midi);
            if (b * bs > sr * 1.5)
                for (int i = 0; i < bs; ++i) { e += buf.getSample (0, i) * buf.getSample (0, i); ++cnt; }
        }
        inst->releaseResources();
        return 10.0 * std::log10 (e / jmax (1, cnt) + 1e-20);
    };
    const double offQuiet = run (false, false), offLoud = run (false, true), onQuiet = run (true, false), onLoud = run (true, true);
    std::cout << "  sidechain: SC off " << String (offQuiet, 1) << " / " << String (offLoud, 1) << " dB (key silent / loud), SC on "
              << String (onQuiet, 1) << " / " << String (onLoud, 1) << " dB\n";
    check (offQuiet > -900.0, String (su->plugin) + ": sidechain bus can be enabled");
    check (std::abs (offLoud - offQuiet) < 0.5, String (su->plugin) + ": key ignored with SC off");
    check (onQuiet - onLoud > 3.0, String (su->plugin) + ": key drives the effect with SC on");
}

/** SI Rack through the real VST3: module loading, chain order (swap) and solo, checked on the audio. */
static void rackTest (VST3PluginFormat& fmt, const PluginDescription& d)
{
    String err;
    auto inst = fmt.createInstanceFromDescription (d, 48000.0, 512, err);
    if (inst == nullptr) { check (false, "rack: load " + err); return; }
    inst->prepareToPlay (48000.0, 512);
    auto set = [&] (const String& name, float norm) { for (auto* p : inst->getParameters()) if (p->getName (64) == name) { p->setValue (norm); return true; } return false; };
    // module list index / 13 (Empty, EQ 73, EQ P1A, FET 76, Opto 2A, Bus VCA, Console, Exciter, Tape, De-Esser, Gate, Transient, Limiter, Width)
    auto level = [&]
    {
        AudioBuffer<float> b (inst->getTotalNumInputChannels(), 512);
        MidiBuffer m;
        double e = 0; int c = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            for (int i = 0; i < 512; ++i) { const float x = 0.5f * std::sin (6.2831853f * 1000.0f * (float) (blk * 512 + i) / 48000.0f); for (int ch = 0; ch < b.getNumChannels(); ++ch) b.setSample (ch, i, x); }
            inst->processBlock (b, m);
            if (blk > 120) for (int i = 0; i < 512; ++i) { e += b.getSample (0, i) * b.getSample (0, i); ++c; }
        }
        return 10.0 * std::log10 (2.0 * e / c + 1e-30);
    };
    // every knob set explicitly (the UI's loadModule does the same with the module defaults)
    auto width = [&] (int slot) { const String sl = "Slot " + String (slot) + " "; return set (sl + "Module", 13.0f / 13.0f) && set (sl + "Knob 1", 0.5f) && set (sl + "Knob 2", 0.0f) && set (sl + "Knob 3", 0.0f); };       // 100 %, bass mono off, -12 dB
    auto limiter = [&] (int slot) { const String sl = "Slot " + String (slot) + " "; return set (sl + "Module", 12.0f / 13.0f) && set (sl + "Knob 1", 0.5f) && set (sl + "Knob 2", 11.0f / 12.0f) && set (sl + "Knob 3", 0.5f); };  // +12 dB, ceiling -1 dB
    bool ok = width (1) && limiter (2);
    check (ok, "rack: parameters found");
    const double widthThenLim = level();
    // swap slots 1 and 2 (what dragging a tile does)
    limiter (1);
    width (2);
    const double limThenWidth = level();
    set ("Slot 2 Solo", 1.0f);
    const double soloWidth = level();
    std::cout << "  rack: Width->Limiter " << String (widthThenLim, 1) << " dBFS, Limiter->Width " << String (limThenWidth, 1) << " dBFS, solo Width " << String (soloWidth, 1) << " dBFS\n";
    check (std::abs (widthThenLim - (-6.0)) < 1.5, "rack: chain order 1 (Width -12 then Limiter +12)");
    check (std::abs (limThenWidth - (-13.0)) < 1.5, "rack: chain order 2 after swap (Limiter then Width)");
    check (std::abs (soloWidth - (-18.0)) < 1.0, "rack: solo plays only the soloed module");
    inst->releaseResources();
}

static void testPlugin (const String& file)
{
    std::cout << "[host] " << file << "\n";
    VST3PluginFormat fmt;
    OwnedArray<PluginDescription> descs;
    fmt.findAllTypesForFile (descs, file);
    check (descs.size() == 1, "plugin descriptions: " + String (descs.size()));
    if (descs.isEmpty()) return;
    sidechainTest (fmt, *descs[0], file);
    if (file.contains ("SI Rack")) rackTest (fmt, *descs[0]);
    if (file.contains ("SI Echo"))
    {
        echoListen (fmt, *descs[0], 0.0f, "single   ");
        echoListen (fmt, *descs[0], 2.0f / 3.0f, "ping-pong");
    }

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 32, 441, 1024 })
        {
            String err;
            auto inst = fmt.createInstanceFromDescription (*descs[0], sr, bs, err);
            check (inst != nullptr, "create failed: " + err);
            if (inst == nullptr) return;
            inst->setPlayConfigDetails (2, 2, sr, bs);
            inst->prepareToPlay (sr, bs);
            AudioBuffer<float> buf (2, bs);
            MidiBuffer midi;
            Random rnd (3);
            bool ok = true;
            auto& params = inst->getParameters();
            for (int b = 0; b < (int) (sr * 3 / bs); ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < bs; ++i) buf.setSample (ch, i, b < 20 ? rnd.nextFloat() - 0.5f : 0.0f);
                if (b % 37 == 0)   // random automation, including mode switches
                    for (auto* p : params) p->setValue (rnd.nextFloat());
                inst->processBlock (buf, midi);
                ok = ok && finite (buf.getReadPointer (0), bs) && finite (buf.getReadPointer (1), bs);
            }
            check (ok, "non-finite output at " + String (sr) + "/" + String (bs));

            // state round trip
            MemoryBlock st;
            inst->getStateInformation (st);
            Array<float> before;
            for (auto* p : params) before.add (p->getValue());
            for (auto* p : params) p->setValue (0.0f);
            inst->processBlock (buf, midi);   // flush queued host->plugin parameter changes
            inst->setStateInformation (st.getData(), (int) st.getSize());
            inst->processBlock (buf, midi);
            // the host-side parameter cache updates asynchronously, so compare the plugin's own state instead
            // skewed ranges may drift by 1 ulp on the first pass (JUCE APVTS), so require a fixed point
            MemoryBlock st1;
            inst->getStateInformation (st1);
            inst->setStateInformation (st1.getData(), (int) st1.getSize());
            inst->processBlock (buf, midi);
            st = st1;
            MemoryBlock st2;
            inst->getStateInformation (st2);
            check (st == st2, "state round trip differs (" + String ((int) st.getSize()) + " vs " + String ((int) st2.getSize()) + " bytes)");
            if (st != st2)
            {
                const auto* a = (const char*) st.getData();
                const auto* b = (const char*) st2.getData();
                size_t i = 0;
                while (i < std::min (st.getSize(), st2.getSize()) && a[i] == b[i]) ++i;
                const size_t from = i > 60 ? i - 60 : 0;
                std::cout << "    A: " << String (a + from, std::min ((size_t) 120, st.getSize() - from)).replaceCharacter ('\0', '.') << "\n"
                          << "    B: " << String (b + from, std::min ((size_t) 120, st2.getSize() - from)).replaceCharacter ('\0', '.') << "\n";
            }

            if (sr == 48000.0 && bs == 441)
            {
                std::cout << "  params " << params.size() << ", latency " << inst->getLatencySamples() << ", state " << (int) st.getSize() << " bytes\n";
                std::unique_ptr<AudioProcessorEditor> ed (inst->createEditorIfNeeded());
                check (ed != nullptr, "no editor");
                if (ed != nullptr)
                {
                    std::cout << "  editor " << ed->getWidth() << "x" << ed->getHeight() << "\n";
                    // the host wraps the plugin's native view, so layout/pixels are checked inside the
                    // plugin instead: set SI_UI_SELFTEST=<dir> (see uiSelfTest in SIWidgets)
                    ed->setSize (ed->getWidth() * 3 / 2, ed->getHeight() * 3 / 2);
                    ed.reset();
                }
            }
            inst->releaseResources();
        }
}

#if SI_TEST_PITCH
#include "PitchEngine.h"
/** Synthetic sung phrase: glottal pulses through three formants, off-key notes with slides and vibrato, an "s" in between. */
static std::vector<float> pitchPhrase (double sr, const std::vector<float>& notes, std::vector<std::pair<int, int>>& spans)
{
    const double noteLen = 0.6, slide = 0.06, sLen = 0.12;
    std::vector<float> y;
    double ph = 0.0;
    Random rnd (11);
    float r1[2] {}, r2[2] {}, r3[2] {};
    auto reson = [sr] (float x, float* z, double f, double bw)
    {
        const double rr = std::exp (-3.14159265358979 * bw / sr), c = 2.0 * rr * std::cos (2.0 * 3.14159265358979 * f / sr);
        const float out = (float) ((1.0 - rr) * x + c * z[0] - rr * rr * z[1]);
        z[1] = z[0]; z[0] = out;
        return out;
    };
    for (size_t k = 0; k < notes.size(); ++k)
    {
        if (k == 2)   // consonant
            for (int i = 0; i < (int) (sLen * sr); ++i) y.push_back (0.05f * (rnd.nextFloat() - 0.5f));
        const int s0 = (int) y.size();
        const int len = (int) (noteLen * sr);
        for (int i = 0; i < len; ++i)
        {
            const double t = i / sr;
            double m = notes[k];
            if (k > 0 && k != 2 && t < slide) m = notes[k - 1] + (notes[k] - notes[k - 1]) * (t / slide);
            if (t > 0.2) m += 0.3 * std::sin (2.0 * 3.14159265358979 * 5.5 * (t - 0.2));
            const double hz = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
            ph += hz / sr;
            const double frac = ph - std::floor (ph);
            const float pulse = (float) (frac < 0.4 ? std::sin (3.14159265358979 * frac / 0.4) : 0.0) - 0.25f;   // open/closed glottis
            const float env = (float) std::min (1.0, std::min (t / 0.02, (noteLen - t) / 0.02));
            float v = reson (pulse, r1, 700, 110) * 1.0f + reson (pulse, r2, 1220, 120) * 0.5f + reson (pulse, r3, 2600, 160) * 0.25f;
            y.push_back (0.6f * v * (k == 0 || k == 2 ? env : std::min (1.0f, (float) ((noteLen - t) / 0.02))));
        }
        spans.push_back ({ s0, (int) y.size() });
    }
    for (int i = 0; i < (int) (0.2 * sr); ++i) y.push_back (0.0f);
    return y;
}

static void testPitch()
{
    using namespace si::pitch;
    std::cout << "[pitch] analysis, note correction, naturalness\n";
    const double sr = 48000.0;
    const std::vector<float> truth { 57.35f, 59.6f, 62.25f, 63.7f };   // A3 +35 ct, C4 -40 ct, D4 +25 ct, E4 -30 ct
    std::vector<std::pair<int, int>> spans;
    auto x = pitchPhrase (sr, truth, spans);
    const int n = (int) x.size();
    float peakAbs = 0.0f;
    for (float v : x) peakAbs = std::max (peakAbs, std::abs (v));
    for (auto& v : x) v *= 0.5f / peakAbs;

    auto A = analyse (x.data(), n, sr);
    String found;
    for (auto& nt : A.notes) found << String (nt.center, 2) << " ";
    std::cout << "  notes found: " << (int) A.notes.size() << " -> " << found << "\n";
    check (A.notes.size() == truth.size(), "pitch: note count " + String ((int) A.notes.size()));
    for (size_t i = 0; i < std::min (A.notes.size(), truth.size()); ++i)
        check (std::abs (A.notes[i].center - truth[i]) < 0.1f, "pitch: centre " + String ((int) i) + " " + String (A.notes[i].center, 3));

    // untouched: identity, and the PSOLA path itself reconstructs the input
    Correction none;
    none.scaleMask = 0xAB5;   // C major
    auto off0 = offsetCurve (A, A.notes, none);
    check (isIdentity (off0), "pitch: no correction = identity");
    std::vector<float> y ((size_t) n);
    {
        const float* ins[1] { x.data() };
        float* outs[1] { y.data() };
        render (A, off0, ins, outs, 1);
        double err = 0, ref = 0;
        for (int i = 0; i < n; ++i) { err += (y[(size_t) i] - x[(size_t) i]) * (y[(size_t) i] - x[(size_t) i]); ref += x[(size_t) i] * x[(size_t) i]; }
        std::cout << "  PSOLA pass-through error " << String (10.0 * std::log10 (err / ref + 1e-30), 1) << " dB\n";
        check (err / ref < 1e-6, "pitch: pass-through exact");
    }

    // correct to C major: centres 100 %, drift 0 %, vibrato untouched
    Correction c = none;
    c.center = 1.0f;
    auto off = offsetCurve (A, A.notes, c);
    {
        const float* ins[1] { x.data() };
        float* outs[1] { y.data() };
        render (A, off, ins, outs, 1);
    }
    auto B = analyse (y.data(), n, sr);
    String got;
    for (auto& nt : B.notes) got << String (nt.center, 2) << " ";
    std::cout << "  after Correct (C major): " << got << "\n";
    const std::vector<float> want { 57.0f, 60.0f, 62.0f, 64.0f };
    check (B.notes.size() == want.size(), "pitch: corrected note count " + String ((int) B.notes.size()));
    for (size_t i = 0; i < std::min (B.notes.size(), want.size()); ++i)
        check (std::abs (B.notes[i].center - want[i]) < 0.07f, "pitch: corrected centre " + String ((int) i) + " " + String (B.notes[i].center, 3));

    // vibrato depth kept (std of pitch around its slow curve inside notes)
    auto vibDepth = [] (const Analysis& an)
    {
        double s = 0; int k = 0;
        for (auto& nt : an.notes)
            for (int f = an.frameOf (nt.start) + 50; f < an.frameOf (nt.end) - 10; ++f)
                if (an.midi[(size_t) f] > 0) { const double d = an.midi[(size_t) f] - an.smooth[(size_t) f]; s += d * d; ++k; }
        return std::sqrt (s / std::max (1, k));
    };
    const double v0 = vibDepth (A), v1 = vibDepth (B);
    std::cout << "  vibrato depth " << String (v0 * 100, 1) << " ct -> " << String (v1 * 100, 1) << " ct\n";
    check (v1 > v0 * 0.8 && v1 < v0 * 1.25, "pitch: vibrato kept");

    // clicks: largest sample step must not grow
    auto maxStep = [] (const std::vector<float>& s) { float m = 0; for (size_t i = 1; i < s.size(); ++i) m = std::max (m, std::abs (s[i] - s[i - 1])); return m; };
    std::cout << "  max sample step in " << String (maxStep (x), 3) << " out " << String (maxStep (y), 3) << "\n";
    check (maxStep (y) < maxStep (x) * 1.5f, "pitch: no clicks");

    // formants stay put when a note is moved 3 semitones up by hand
    auto moved = A.notes;
    moved[0].manual = true;
    moved[0].target = moved[0].center + 3.0f;
    auto off3 = offsetCurve (A, moved, none);
    {
        const float* ins[1] { x.data() };
        float* outs[1] { y.data() };
        render (A, off3, ins, outs, 1);
    }
    // spectral centroid (300 Hz - 4 kHz) of the moved note: a plain resample would raise it ~19 % (+3 st)
    auto centroid = [sr] (const std::vector<float>& s, int a, int b)
    {
        constexpr int order = 13, nf = 1 << order;
        juce::dsp::FFT f (order);
        double num = 0, den = 0;
        for (int p = a; p + nf <= b; p += nf / 4)
        {
            std::vector<float> buf ((size_t) nf * 2, 0.0f);
            for (int i = 0; i < nf; ++i) buf[(size_t) i] = s[(size_t) (p + i)] * (0.5f - 0.5f * std::cos (6.2831853f * i / (nf - 1)));
            f.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = (int) (300.0 * nf / sr); k < (int) (4000.0 * nf / sr); ++k)
            {
                const double pw = (double) buf[(size_t) k] * buf[(size_t) k];
                num += pw * k * sr / nf; den += pw;
            }
        }
        return num / (den + 1e-30);
    };
    const int a = spans[0].first + (int) (0.05 * sr), b = spans[0].second - (int) (0.05 * sr);
    const double cIn = centroid (x, a, b), cOut = centroid (y, a, b);
    std::cout << "  spectral centroid of the moved note (+3 st): " << String (cIn, 0) << " Hz -> " << String (cOut, 0) << " Hz ("
              << String (100.0 * (cOut / cIn - 1.0), 1) << " %, plain resample would be +18.9 %)\n";
    check (std::abs (cOut / cIn - 1.0) < 0.07, "pitch: formants preserved");
    auto C = analyse (y.data(), n, sr);
    if (! C.notes.empty()) std::cout << "  moved note now " << String (C.notes[0].center, 2) << " (want " << String (A.notes[0].center + 3.0f, 2) << ")\n";
    check (! C.notes.empty() && std::abs (C.notes[0].center - (A.notes[0].center + 3.0f)) < 0.08f, "pitch: manual move");
}

#include "../plugins/SIPitch/PluginProcessor.h"
struct FakeHead : public AudioPlayHead
{
    PositionInfo info;
    Optional<PositionInfo> getPosition() const override { return info; }
};


struct PhraseOpts
{
    double vib = 0.3, slide = 0.06, breath = 0.0, h2 = 0.0;
    std::vector<double> dur, level;
};
/** Like pitchPhrase, but every note has its own length / level; breath noise and a strong 2nd harmonic optional. */
static std::vector<float> phraseEx (double sr, const std::vector<float>& notes, const PhraseOpts& o)
{
    std::vector<float> y;
    double ph = 0.0;
    Random rnd (21);
    float r1[2] {}, r2[2] {}, r3[2] {};
    auto reson = [sr] (float x, float* z, double f, double bw)
    {
        const double rr = std::exp (-3.14159265358979 * bw / sr), c = 2.0 * rr * std::cos (2.0 * 3.14159265358979 * f / sr);
        const float out = (float) ((1.0 - rr) * x + c * z[0] - rr * rr * z[1]);
        z[1] = z[0]; z[0] = out;
        return out;
    };
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const double len = o.dur.empty() ? 0.6 : o.dur[k];
        const double lvl = o.level.empty() ? 1.0 : o.level[k];
        for (int i = 0; i < (int) (len * sr); ++i)
        {
            const double t = i / sr;
            double m = notes[k];
            if (k > 0 && t < o.slide) m = notes[k - 1] + (notes[k] - notes[k - 1]) * (t / o.slide);
            if (t > 0.18) m += o.vib * std::sin (2.0 * 3.14159265358979 * 5.5 * (t - 0.18));
            const double hz = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
            ph += hz / sr;
            const double frac = ph - std::floor (ph);
            float pulse = (float) (frac < 0.4 ? std::sin (3.14159265358979 * frac / 0.4) : 0.0) - 0.25f;
            pulse += (float) (o.h2 * std::sin (2.0 * 3.14159265358979 * 2.0 * frac + 0.7));
            pulse += (float) (o.breath * (rnd.nextFloat() - 0.5f) * 2.0f);
            float v = reson (pulse, r1, 650, 110) + reson (pulse, r2, 1150, 120) * 0.5f + reson (pulse, r3, 2500, 160) * 0.25f;
            const double env = std::min (1.0, (len - t) / 0.015) * (k == 0 ? std::min (1.0, t / 0.02) : 1.0);
            y.push_back ((float) (0.5 * lvl * env) * v);
        }
    }
    for (int i = 0; i < (int) (0.2 * sr); ++i) y.push_back (0.0f);
    float pk = 0.0f;
    for (float v : y) pk = std::max (pk, std::abs (v));
    for (auto& v : y) v *= 0.5f / pk;
    return y;
}

static void testPitchStress()
{
    using namespace si::pitch;
    std::cout << "[pitch] hard vocals\n";
    const double sr = 48000.0;
    struct Case { const char* name; std::vector<float> notes; PhraseOpts o; };
    std::vector<Case> cases;
    { Case c { "low male, strong 2nd harmonic", { 45.4f, 47.6f, 50.3f, 52.65f }, {} }; c.o.h2 = 0.9; cases.push_back (c); }
    { Case c { "high female", { 69.35f, 71.6f, 74.3f, 76.65f }, {} }; cases.push_back (c); }
    { Case c { "fast run 120 ms", { 62.3f, 64.35f, 65.7f, 67.35f, 69.3f, 67.6f, 65.7f, 64.4f }, {} }; c.o.dur = std::vector<double> (8, 0.12); c.o.slide = 0.03; c.o.vib = 0.0; cases.push_back (c); }
    { Case c { "wide vibrato +-0.8 st", { 60.3f, 64.4f, 67.35f }, {} }; c.o.vib = 0.8; c.o.dur = { 1.2, 1.2, 1.2 }; cases.push_back (c); }
    { Case c { "breathy", { 57.35f, 59.6f, 62.25f, 63.7f }, {} }; c.o.breath = 0.35; cases.push_back (c); }
    { Case c { "quiet next to loud", { 57.35f, 59.6f, 62.25f, 63.7f }, {} }; c.o.level = { 1.0, 0.06, 1.0, 0.06 }; cases.push_back (c); }
    for (auto& cs : cases)
    {
        auto x = phraseEx (sr, cs.notes, cs.o);
        const int n = (int) x.size();
        auto A = analyse (x.data(), n, sr);
        String found;
        float worstIn = 0.0f;
        for (size_t i = 0; i < A.notes.size(); ++i)
        {
            found << String (A.notes[i].center, 2) << " ";
            if (i < cs.notes.size()) worstIn = std::max (worstIn, std::abs (A.notes[i].center - cs.notes[i]));
        }
        Correction c;
        c.scaleMask = 0xFFF;   // chromatic: every note goes to its nearest semitone
        c.center = 1.0f;
        auto off = offsetCurve (A, A.notes, c);
        std::vector<float> y ((size_t) n);
        const float* ins[1] { x.data() };
        float* outs[1] { y.data() };
        render (A, off, ins, outs, 1);
        auto B = analyse (y.data(), n, sr);
        float worstOut = 0.0f;
        String got;
        for (size_t i = 0; i < B.notes.size(); ++i)
        {
            got << String (B.notes[i].center, 2) << " ";
            if (i < cs.notes.size()) worstOut = std::max (worstOut, std::abs (B.notes[i].center - std::round (cs.notes[i])));
        }
        // roughness: pitch jitter (frame-to-frame second difference) inside notes, out vs in
        auto jitter = [] (const Analysis& an)
        {
            double s = 0; int k = 0;
            for (auto& nt : an.notes)
                for (int f = an.frameOf (nt.start) + 4; f < an.frameOf (nt.end) - 4; ++f)
                    if (an.midi[(size_t) f - 1] > 0 && an.midi[(size_t) f] > 0 && an.midi[(size_t) f + 1] > 0)
                    { const double d = an.midi[(size_t) f + 1] - 2.0 * an.midi[(size_t) f] + an.midi[(size_t) f - 1]; s += d * d; ++k; }
            return std::sqrt (s / std::max (1, k)) * 100.0;
        };
        std::cout << "  " << String (cs.name).paddedRight (' ', 30) << " in: " << found << "| out: " << got
                  << "| worst " << String (worstIn * 100, 0) << " / " << String (worstOut * 100, 0) << " ct, jitter "
                  << String (jitter (A), 2) << " -> " << String (jitter (B), 2) << " ct\n";
        if (A.notes.size() != cs.notes.size())
        {
            String dbg;
            for (auto& nt : A.notes) dbg << roundToInt (nt.start / 48.0) << "-" << roundToInt (nt.end / 48.0) << "ms:" << String (nt.center, 2) << " ";
            std::cout << "    notes: " << dbg << "\n";
        }
        check (A.notes.size() == cs.notes.size() && worstIn < 0.12f, String ("pitch stress: analysis - ") + cs.name);
        check (B.notes.size() == cs.notes.size() && worstOut < 0.10f, String ("pitch stress: correction - ") + cs.name);
        check (jitter (B) < jitter (A) * 1.6 + 0.5, String ("pitch stress: roughness - ") + cs.name);
    }
}
/** End to end on the real processor: Transfer against a moving timeline, Correct, playback in place, project reload. */
static void testPitchPlugin()
{
    std::cout << "[pitch] plugin: transfer, correct, playback in place, project reload\n";
    const double sr = 48000.0;
    const int bs = 512;
    std::vector<std::pair<int, int>> spans;
    auto x = pitchPhrase (sr, { 57.35f, 59.6f, 62.25f, 63.7f }, spans);
    float pk = 0.0f;
    for (float v : x) pk = std::max (pk, std::abs (v));
    for (auto& v : x) v *= 0.5f / pk;
    const int n = (int) x.size();
    const int64 start = 96000;   // the vocal starts 2 s into the song

    FakeHead head;
    head.info.setBpm (120.0);
    AudioBuffer<float> buf (2, bs);
    MidiBuffer midi;
    auto block = [&] (SIPitchProcessor& p, int64 pos, bool play, const float* src, float fill, int cnt, std::vector<float>* out)
    {
        buf.setSize (2, cnt, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < cnt; ++i) buf.setSample (ch, i, src != nullptr ? src[i] : fill);
        head.info.setIsPlaying (play);
        head.info.setTimeInSamples (pos);
        p.processBlock (buf, midi);
        if (out != nullptr) for (int i = 0; i < cnt; ++i) out->push_back (buf.getSample (0, i));
    };
    auto waitReady = [] (SIPitchProcessor& p)
    {
        for (int k = 0; k < 3000; ++k)
        {
            p.pollForTests();
            if (p.getStatus() == SIPitchProcessor::Status::Ready && ! p.isRendering()) { Thread::sleep (5); p.pollForTests(); return true; }
            Thread::sleep (10);
        }
        return false;
    };
    auto playBack = [&] (SIPitchProcessor& p)
    {
        std::vector<float> y;
        for (int64 pos = start - 4800; pos < start + n; pos += bs)
            block (p, pos, true, nullptr, 0.25f, (int) std::min ((int64) bs, start + n - pos), &y);
        return y;
    };
    auto centres = [&] (const std::vector<float>& y)
    {
        auto an = si::pitch::analyse (y.data() + 4800, n, sr);
        std::vector<float> c;
        for (auto& nt : an.notes) c.push_back (nt.center);
        return c;
    };
    auto str = [] (const std::vector<float>& v) { String s; for (float f : v) s << String (f, 2) << " "; return s; };

    auto proc = std::make_unique<SIPitchProcessor>();
    proc->prepareToPlay (sr, bs);
    proc->setPlayHead (&head);
    proc->setArmed (true);
    for (int i = 0; i < n; i += bs) block (*proc, start + i, true, x.data() + i, 0.0f, std::min (bs, n - i), nullptr);
    block (*proc, start + n, false, nullptr, 0.0f, bs, nullptr);   // DAW stops
    const bool ready = waitReady (*proc);
    check (ready, "pitch plugin: transfer + analysis finished");
    if (! ready) return;
    auto tk = proc->getTake();
    std::cout << "  transfer: " << tk->audio.getNumSamples() << " samples at " << tk->hostStart << ", " << (int) proc->getNotes().size() << " notes\n";
    check (tk->hostStart == start && tk->audio.getNumSamples() == n, "pitch plugin: take placed on the timeline");
    check (proc->getNotes().size() == 4, "pitch plugin: notes");

    // Correct (C major is the default key)
    proc->apvts.getParameter ("center")->setValueNotifyingHost (1.0f);
    waitReady (*proc);
    auto y = playBack (*proc);
    bool thru = true;
    for (int i = 0; i < 4800; ++i) thru = thru && std::abs (y[(size_t) i] - 0.25f) < 1e-6f;
    check (thru, "pitch plugin: input passes through outside the take");
    const auto c1 = centres (y);
    std::cout << "  played back after Correct: " << str (c1) << "\n";
    const std::vector<float> want { 57.0f, 60.0f, 62.0f, 64.0f };
    check (c1.size() == 4, "pitch plugin: corrected playback notes");
    for (size_t i = 0; i < std::min (c1.size(), want.size()); ++i) check (std::abs (c1[i] - want[i]) < 0.08f, "pitch plugin: corrected note " + String ((int) i));

    // a hand edit, then save / reload the project
    auto ns = proc->getNotes();
    ns[1].manual = true;
    ns[1].target = 61.0f;
    proc->setNotes (ns);
    waitReady (*proc);
    MemoryBlock st;
    proc->getStateInformation (st);
    const File takeFile = tk->file;
    proc.reset();

    auto p2 = std::make_unique<SIPitchProcessor>();
    p2->prepareToPlay (sr, bs);
    p2->setPlayHead (&head);
    p2->setStateInformation (st.getData(), (int) st.getSize());
    check (waitReady (*p2), "pitch plugin: project reload finished");
    const auto ns2 = p2->getNotes();
    check (ns2.size() == 4 && ns2[1].manual && std::abs (ns2[1].target - 61.0f) < 1e-3f, "pitch plugin: hand edit restored");
    check (p2->apvts.getRawParameterValue ("center")->load() > 99.0f, "pitch plugin: Correct restored");
    const auto c2 = centres (playBack (*p2));
    std::cout << "  after reload: " << str (c2) << "\n";
    check (c2.size() == 4 && std::abs (c2[1] - 61.0f) < 0.08f, "pitch plugin: hand-moved note plays back");
    p2.reset();
    takeFile.deleteFile();

    // drag & drop: a 44.1 kHz file dropped while the playhead rests at 3 s in a 48 kHz project
    {
        const double fileRate = 44100.0;
        std::vector<std::pair<int, int>> sp2;
        auto z = pitchPhrase (fileRate, { 57.35f, 59.6f, 62.25f, 63.7f }, sp2);
        float zp = 0.0f;
        for (float v : z) zp = std::max (zp, std::abs (v));
        AudioBuffer<float> zb (1, (int) z.size());
        for (int i = 0; i < (int) z.size(); ++i) zb.setSample (0, i, z[(size_t) i] * 0.5f / zp);
        auto wavFile = File::getSpecialLocation (File::tempDirectory).getChildFile ("si-pitch-drop.wav");
        wavFile.deleteFile();
        {
            WavAudioFormat wav;
            if (auto os = wavFile.createOutputStream())
                if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.get(), fileRate, 1, 24, {}, 0) }) { os.release(); w->writeFromAudioSampleBuffer (zb, 0, zb.getNumSamples()); }
        }
        auto p3 = std::make_unique<SIPitchProcessor>();
        p3->prepareToPlay (sr, bs);
        p3->setPlayHead (&head);
        block (*p3, 144000, false, nullptr, 0.0f, bs, nullptr);   // stopped, playhead at 3 s
        check (p3->loadFile (wavFile), "pitch plugin: drop accepted");
        check (waitReady (*p3), "pitch plugin: dropped file analysed");
        if (auto t3 = p3->getTake())
        {
            const int expectLen = (int) std::floor (z.size() * sr / fileRate) - 4;
            String c3;
            for (auto& nt : p3->getNotes()) c3 << String (nt.center, 2) << " ";
            std::cout << "  dropped 44.1 kHz file: at " << t3->hostStart << ", " << t3->audio.getNumSamples() << " samples (want " << expectLen << "), notes " << c3 << "\n";
            check (t3->hostStart == 144000 && std::abs (t3->audio.getNumSamples() - expectLen) <= 2, "pitch plugin: dropped take placed + resampled");
            const auto n3 = p3->getNotes();
            check (n3.size() == 4 && std::abs (n3[0].center - 57.35f) < 0.1f && std::abs (n3[3].center - 63.7f) < 0.1f, "pitch plugin: dropped take pitch");
            t3->file.deleteFile();
        }
        p3.reset();
        wavFile.deleteFile();
    }
}

/** SITests --pitch <file> <key 0-11> <minor 0/1> [out.wav]: SI Pitch on a real vocal, with numbers. */
static int pitchFileReport (const String& path, int key, bool minor, const String& outPath)
{
    using namespace si::pitch;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (path)));
    if (rd == nullptr) { std::cout << "cannot read " << path << "\n"; return 1; }
    const int n = (int) rd->lengthInSamples;
    const double sr = rd->sampleRate;
    AudioBuffer<float> a (2, n);
    rd->read (&a, 0, n, 0, true, true);
    if (rd->numChannels == 1) a.copyFrom (1, 0, a, 0, 0, n);
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (a.getSample (0, i) + a.getSample (1, i));
    const double t0 = Time::getMillisecondCounterHiRes();
    auto A = analyse (mono.data(), n, sr);
    const double tA = Time::getMillisecondCounterHiRes() - t0;
    int voiced = 0;
    for (float v : A.midi) voiced += v > 0.0f;
    std::vector<float> lens, offKey;
    Correction c;
    const std::vector<int> iv = minor ? std::vector<int> { 0, 2, 3, 5, 7, 8, 10 } : std::vector<int> { 0, 2, 4, 5, 7, 9, 11 };
    c.scaleMask = 0;
    for (int v : iv) c.scaleMask |= 1 << ((key + v) % 12);
    for (auto& nt : A.notes)
    {
        lens.push_back ((float) (nt.end - nt.start) / (float) sr * 1000.0f);
        offKey.push_back (std::abs (nt.center - nearestInScale (nt.center, c.scaleMask)) * 100.0f);
    }
    auto pct = [] (std::vector<float> v, double p) { if (v.empty()) return 0.0f; std::sort (v.begin(), v.end()); return v[(size_t) std::min ((double) v.size() - 1, p * v.size())]; };
    int shortN = 0;
    for (float l : lens) shortN += l < 80.0f;
    std::cout << "file " << File (path).getFileName() << ": " << String (n / sr, 1) << " s, voiced " << (100 * voiced / (int) A.midi.size()) << "%, analysis " << String (tA / 1000.0, 2) << " s\n";
    {
        double lr = 0, ll = 0, rr = 0;
        for (int i = 0; i < n; ++i) { const double l = a.getSample (0, i), r = a.getSample (1, i); lr += l * r; ll += l * l; rr += r * r; }
        // pitch-mark spacing vs the tracked period
        std::vector<float> mj;
        for (size_t k = 1; k + 1 < A.marks.size(); ++k)
            if (A.markVoiced[k] && A.markVoiced[k + 1])
            {
                const float mm = A.midi[(size_t) A.frameOf (A.marks[k])];
                if (mm <= 0.0f) continue;
                const float P = (float) (sr / (440.0 * std::pow (2.0, (mm - 69.0) / 12.0)));
                mj.push_back (std::abs ((A.marks[k + 1] - A.marks[k]) - P) / P * 100.0f);
            }
        std::cout << "  L/R correlation " << String (lr / std::sqrt (ll * rr + 1e-30), 3) << ", pitch-mark spacing error median " << String (pct (mj, 0.5), 1)
                  << "% 90% " << String (pct (mj, 0.9), 1) << "% (of a period)\n";
    }
    int spoken = 0;
    for (auto& nt : A.notes) spoken += nt.sung < 0.5f;
    std::cout << "  spoken / gliding syllables (Correct mostly leaves them): " << spoken << "\n";
    std::cout << "  notes " << (int) A.notes.size() << ", length median " << String (pct (lens, 0.5), 0) << " ms (10% " << String (pct (lens, 0.1), 0)
              << ", 90% " << String (pct (lens, 0.9), 0) << "), shorter than 80 ms: " << shortN << "\n";
    std::cout << "  sung off the key (ct): median " << String (pct (offKey, 0.5), 0) << ", 90% " << String (pct (offKey, 0.9), 0) << ", max " << String (pct (offKey, 1.0), 0) << "\n";

    c.center = 1.0f;
    c.drift = 0.3f;
    auto off = offsetCurve (A, A.notes, c);
    AudioBuffer<float> y (2, n);
    const float* ins[2] { a.getReadPointer (0), a.getReadPointer (1) };
    float* outs[2] { y.getWritePointer (0), y.getWritePointer (1) };
    const double t1 = Time::getMillisecondCounterHiRes();
    render (A, off, ins, outs, 2);
    const double tR = Time::getMillisecondCounterHiRes() - t1;
    std::vector<float> ym ((size_t) n);
    for (int i = 0; i < n; ++i) ym[(size_t) i] = 0.5f * (y.getSample (0, i) + y.getSample (1, i));
    auto B = analyse (ym.data(), n, sr);
    // every corrected input note: measured output pitch (median over its middle) vs where it should be
    std::vector<float> err;
    int bad = 0;
    for (auto& nt : A.notes)
    {
        const int f0 = A.frameOf (nt.start), f1 = A.frameOf (nt.end), tr = (f1 - f0) / 5;
        std::vector<float> v;
        for (int f = f0 + tr; f < f1 - tr; ++f) if (B.midi[(size_t) f] > 0.0f) v.push_back (B.midi[(size_t) f]);
        if (v.size() < 4) continue;
        std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
        const float e = std::abs (v[v.size() / 2] - correctedCenter (nt, c)) * 100.0f;
        err.push_back (e);
        bad += e > 20.0f;
    }
    std::cout << "  after Correct: off target median " << String (pct (err, 0.5), 1) << " ct, 90% " << String (pct (err, 0.9), 1) << " ct, notes > 20 ct off: " << bad << "/" << (int) err.size()
              << ", render " << String (tR / 1000.0, 2) << " s\n";
    // render accuracy frame by frame: output pitch vs input pitch + requested offset
    {
        std::vector<float> fe;
        int bigF = 0, octF = 0, lost = 0;
        for (size_t f = 0; f < A.midi.size() && f < B.midi.size(); ++f)
        {
            bool inNote = false;
            for (auto& nt : A.notes) if ((int) f >= A.frameOf (nt.start) + 2 && (int) f <= A.frameOf (nt.end) - 2) { inNote = true; break; }
            if (! inNote || A.midi[f] <= 0.0f) continue;
            if (B.midi[f] <= 0.0f) { ++lost; continue; }
            const float e = std::abs (B.midi[f] - (A.midi[f] + off[f])) * 100.0f;
            if (e > 600.0f) { ++octF; continue; }
            fe.push_back (e);
            bigF += e > 20.0f;
        }
        std::cout << "  frame accuracy: median " << String (pct (fe, 0.5), 1) << " ct, 90% " << String (pct (fe, 0.9), 1) << " ct, >20 ct " << String (100.0 * bigF / std::max<size_t> (1, fe.size()), 1)
                  << "% frames, octave jumps " << octF << ", voicing lost " << lost << " of " << (int) fe.size() + octF + lost << "\n";
    }
    float stepIn = 0, stepOut = 0;
    for (int i = 1; i < n; ++i) { stepIn = std::max (stepIn, std::abs (mono[(size_t) i] - mono[(size_t) i - 1])); stepOut = std::max (stepOut, std::abs (ym[(size_t) i] - ym[(size_t) i - 1])); }
    std::cout << "  largest sample step in " << String (stepIn, 3) << " out " << String (stepOut, 3) << "\n";
    if (outPath.isNotEmpty())
    {
        WavAudioFormat wav;
        File o (outPath);
        o.deleteFile();
        if (auto os = o.createOutputStream())
            if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.get(), sr, 2, 24, {}, 0) }) { os.release(); w->writeFromAudioSampleBuffer (y, 0, n); }
    }
    return 0;
}
#endif


#if SI_TEST_ECHO
/** SITests --echo <in> <out> <mode 0-3> <ms> <feedback %> <lowcut> <highcut>: wet-only render (send) for comparisons. */
static int echoRender (const String& in, const String& outPath, int mode, double ms, float fbPct, float lowCut, float highCut)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (in)));
    if (rd == nullptr) return 1;
    const double sr = rd->sampleRate;
    const int n = (int) std::min<int64> (rd->lengthInSamples, (int64) (sr * 30.0));
    AudioBuffer<float> a (2, n + (int) (sr * 4));
    a.clear();
    rd->read (&a, 0, n, 0, true, true);
    if (rd->numChannels == 1) a.copyFrom (1, 0, a, 0, 0, n);
    si::echo::EchoEngine e;
    si::echo::EchoParams p;
    p.mode = (si::echo::Mode) mode; p.timeL = p.timeR = ms / 1000.0; p.feedback = fbPct / 100.0f; p.mix = 1.0f;
    p.lowCut = lowCut; p.highCut = highCut; p.style = 0; p.drive = 0.0f;
    e.setParams (p);
    e.prepare (sr);
    for (int i = 0; i < a.getNumSamples(); i += 512) e.process (a.getWritePointer (0) + i, a.getWritePointer (1) + i, std::min (512, a.getNumSamples() - i));
    WavAudioFormat wav;
    File o (outPath);
    o.deleteFile();
    if (auto os = o.createOutputStream())
        if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.get(), sr, 2, 24, {}, 0) }) { os.release(); w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()); }
    return 0;
}
#endif

/** SITests --reverb <in> <out>: the user's VintageVerb setting (Smooth Random, Now) on 20 s of <in> + 8 s tail, wet only. */
static int reverbRender (const String& in, const String& outPath)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> rd (fm.createReaderFor (File (in)));
    if (rd == nullptr) return 1;
    const double sr = rd->sampleRate;
    const int n = (int) std::min<int64> (rd->lengthInSamples, (int64) (sr * 20.0));
    AudioBuffer<float> a (2, n + (int) (sr * 8));
    a.clear();
    rd->read (&a, 0, n, 0, true, true);
    if (rd->numChannels == 1) a.copyFrom (1, 0, a, 0, 0, n);
    si::dsp::ReverbEngine e;
    e.prepare (sr, 512);
    si::dsp::ReverbParams p;
    p.mode = 13; p.color = 2; p.decay = 2.72f; p.size = 0.864f; p.attack = 0.5f; p.earlyDiff = 1.0f; p.lateDiff = 1.0f;
    p.modRate = 2.53f; p.modDepth = 0.38f; p.highCut = 8290.0f; p.lowCut = 210.0f; p.bassMult = 1.5f; p.bassXover = 700.0f;
    p.highShelf = -24.0f; p.highFreq = 6000.0f; p.preDelayMs = 28.23f; p.width = 1.0f;
    e.setParams (p);
    for (int i = 0; i < a.getNumSamples(); i += 512) e.process (a.getWritePointer (0) + i, a.getWritePointer (1) + i, std::min (512, a.getNumSamples() - i));
    WavAudioFormat wav;
    File o (outPath);
    o.deleteFile();
    if (auto os = o.createOutputStream())
        if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.get(), sr, 2, 24, {}, 0) }) { os.release(); w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()); }
    return 0;
}
int pluginList (const String&, const String&);
int stateDump (const String&, const String&, const String&);
int fetGain (float, float, int);
int compMeasure (const String&, const String&, const String&, const String&, const String&);

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    if (argc >= 5 && String (argv[1]) == "--fet") return fetGain ((float) atof (argv[2]), (float) atof (argv[3]), atoi (argv[4]));
    if (argc >= 5 && String (argv[1]) == "--state") return stateDump (String (CharPointer_UTF8 (argv[2])), String (CharPointer_UTF8 (argv[3])), String (CharPointer_UTF8 (argv[4])));
    if (argc >= 3 && String (argv[1]) == "--list") return pluginList (String (CharPointer_UTF8 (argv[2])), argc >= 4 ? String (CharPointer_UTF8 (argv[3])) : String());
    if (argc >= 5 && String (argv[1]) == "--comp") return compMeasure (String (CharPointer_UTF8 (argv[2])), String (CharPointer_UTF8 (argv[3])), String (CharPointer_UTF8 (argv[4])), argc >= 6 ? String (CharPointer_UTF8 (argv[5])) : String (CharPointer_UTF8 (argv[3])), argc >= 7 ? String (argv[6]) : String());
    if (argc >= 4 && String (argv[1]) == "--reverb") return reverbRender (String (CharPointer_UTF8 (argv[2])), String (CharPointer_UTF8 (argv[3])));
#if SI_TEST_ECHO
    if (argc >= 9 && String (argv[1]) == "--echo")
        return echoRender (String (CharPointer_UTF8 (argv[2])), String (CharPointer_UTF8 (argv[3])), atoi (argv[4]), atof (argv[5]), (float) atof (argv[6]), (float) atof (argv[7]), (float) atof (argv[8]));
#endif
#if SI_TEST_PITCH
    if (argc >= 5 && String (argv[1]) == "--pitch")
        return pitchFileReport (String (CharPointer_UTF8 (argv[2])), atoi (argv[3]), atoi (argv[4]) != 0, argc >= 6 ? String (CharPointer_UTF8 (argv[5])) : String());
#endif
    testReverbModes();
    testReverbQuality();
#if SI_TEST_PITCH
    testPitch();
    testPitchStress();
    testPitchPlugin();
#endif
    testRiser();
#if SI_TEST_EQ
    testEq();
#endif
#if SI_TEST_TUNE
    testTune();
    testTuneAllKeys();
    testTuneNatural();
#endif
#if SI_TEST_SAT
    testSaturn();
#endif
#if SI_TEST_ECHO
    testEchoRack();
#endif
    {
        // demo vocal for the plugins' UI self-test (SI_UI_SELFTEST_AUDIO)
        auto f = File::getCurrentWorkingDirectory().getChildFile ("selftest-vocal.wav");
        f.deleteFile();
        auto v = fakeVocal (48000.0);
        WavAudioFormat wav;
        if (auto os = f.createOutputStream())
            if (std::unique_ptr<AudioFormatWriter> w { wav.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0) })
            {
                os.release();
                w->writeFromAudioSampleBuffer (v, 0, v.getNumSamples());
            }
#if JUCE_WINDOWS
        _putenv_s ("SI_UI_SELFTEST_AUDIO", f.getFullPathName().toRawUTF8());
#else
        setenv ("SI_UI_SELFTEST_AUDIO", f.getFullPathName().toRawUTF8(), 1);
#endif
    }
    for (int i = 1; i < argc; ++i) testPlugin (String (CharPointer_UTF8 (argv[i])));
    std::cout << (failures == 0 ? "ALL TESTS PASSED\n" : String (failures) + " FAILURES\n");
    return failures == 0 ? 0 : 1;
}
