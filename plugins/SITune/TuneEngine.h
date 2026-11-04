#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <cmath>

namespace si::tune
{
struct TuneParams
{
    int scaleMask = 0xFFF;     // bit n = pitch class n (C = 0) allowed
    float speedMs = 20.0f;     // retune time, 0 = hard tune
    float amount = 1.0f;       // 0..1
    float humanize = 0.0f;     // 0..1: lets small deviations (vibrato) through
    int stabilizer = 1;        // 0 none, 1 short, 2 mid, 3 long
    float transpose = 0.0f;    // semitones
    float refHz = 440.0f;
    bool lowLatency = true;
    int inputType = 1;         // 0 Soprano, 1 Alto/Tenor, 2 Low Male, 3 Instrument, 4 Bass Instrument
    float tracking = 0.5f;     // 0 strict .. 1 relaxed pitch detection
    float flex = 0.0f;         // 0..1 Flex Tune: only correct notes that are already close to the target
    float vibratoDb = 0.0f;    // Natural Vibrato, -12..+12 dB scales the singer's own vibrato
};

/** Monophonic real-time pitch corrector.
    Detection: YIN on a 4x decimated signal, refined at full rate.
    Shifting: pitch-synchronous delay line (the read head jumps by one detected period with a short
    crossfade), so latency stays around 2-5 ms. */
class TuneEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const TuneParams& p) { prm = p; }
    void process (float* l, float* r, int n);
    int latencySamples() const;

    // live values for the UI
    std::atomic<float> inMidi { -1.0f }, outMidi { -1.0f }, targetMidi { -1.0f }, level { 0.0f }, confidence { 0.0f };
    static constexpr int kHist = 256;
    std::array<std::atomic<float>, kHist> histIn {}, histOut {};
    std::atomic<int> histPos { 0 };

private:
    void detect();
    float nearestAllowed (float midi) const;

    double sr = 48000.0;
    TuneParams prm;

    // decimated analysis
    static constexpr int kDec = 4, kDecBuf = 1024;
    std::array<float, kDecBuf> dec {};
    int decW = 0, decCount = 0, sinceHop = 0;
    float lp1 = 0, lp2 = 0;
    // full-rate history (refinement + shifter)
    static constexpr int kBuf = 16384;
    std::vector<float> bufL, bufR;
    int w = 0;

    // detection state
    float period = 0.0f;        // full-rate samples, 0 = unvoiced
    float midiNow = -1.0f;
    int heldNote = -1, candNote = -1;
    float candTime = 0.0f;
    float shift = 0.0f;         // current smoothed shift, semitones
    float shiftTarget = 0.0f;
    float rmsEnv = 0.0f;
    float midiSlow = -1.0f;     // slow pitch average (vibrato = midiNow - midiSlow)
    float midiPrev = -1.0f, velSm = 0.0f;   // pitch movement (semitones / s) for Flex Tune
    float holdTime = 0.0f;      // how long the current note has been held (Humanize)
    float speedEff = 10.0f;     // retune time after Humanize

    // shifter
    double D = 96.0, D2 = 96.0;
    float fade = 1.0f, fadeInc = 0.0f;
    float ratio = 1.0f;
};
} // namespace si::tune
