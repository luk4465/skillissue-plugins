#pragma once
#include "DspUtils.h"
#include <array>

namespace si::dsp
{
enum class ModType { Sine, Random, Chorus, Chaotic };
enum class Family { Hall, Room, Plate, Chamber, Huge, Tiny };

struct ModeDef
{
    const char* name;
    const char* blurb;
    Family family;
    float sizeMul;      // scales tank delays
    float decayMul;     // scales RT60
    float inDiff;       // input diffusion amount multiplier
    float lateDiff;     // in-loop diffusion multiplier
    ModType mod;
    float modDepthMul, modRateMul;
    float erMix;        // early reflections in output
    float drive;        // in-loop saturation (dirty modes)
    float hfDamp;       // extra in-loop high damping 0..1
    float spread;       // stereo decorrelation 0..1
    float density;      // output tap density 0..1
    bool nonlin;        // gated / reverse envelope instead of tank
    float autoPreMs;    // tempo AUTO: target pre-delay
    float autoDecayBeats; // tempo AUTO: target decay at size 0.5
};

constexpr int kNumModes = 22;
const ModeDef& getMode (int index);

enum class Color { Seventies = 0, Eighties = 1, Now = 2 };

struct ReverbParams
{
    int mode = 0;
    int color = 2;
    float decay = 2.5f;       // RT60 seconds
    float size = 0.6f;        // 0..1
    float attack = 0.3f;      // 0..1
    float earlyDiff = 0.7f;   // 0..1
    float lateDiff = 0.7f;    // 0..1
    float modRate = 0.6f;     // Hz
    float modDepth = 0.4f;    // 0..1
    float highCut = 9000.0f;  // output LP
    float lowCut = 80.0f;     // output HP
    float bassMult = 1.2f;    // low RT60 multiplier
    float bassXover = 400.0f;
    float highShelf = -6.0f;  // in-loop HF damping dB (per RT)
    float highFreq = 5000.0f;
    float preDelayMs = 20.0f;
    float width = 1.0f;       // 0..1.5
    bool freeze = false;
};

/** Stereo algorithmic reverb, 100 % wet output. Used by SI Reverb (realtime) and SI Riser (offline). */
class ReverbEngine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    void setParams (const ReverbParams& p);
    /** Processes in place: L/R in -> wet L/R out. */
    void process (float* left, float* right, int numSamples);
    /** Wet envelope (for UI meters), 0..1-ish. */
    float getEnvelope() const { return envOut; }

private:
    static constexpr int N = 8;
    static constexpr int kInAP = 4;
    static constexpr int kErTaps = 12;
    static constexpr int kNlTaps = 24;

    void updateCoefficients();
    float sr = 48000.0f;
    ReverbParams prm;
    bool dirty = true;

    // pre-delay + input
    DelayLine preL, preR;
    Smoother preSm;
    OnePole bwL, bwR;                 // colour bandwidth on input
    std::array<Allpass, kInAP> apL, apR;
    std::array<float, kInAP> apBaseMs { 4.71f, 3.59f, 12.73f, 9.31f };

    // attack smear / early reflections
    DelayLine erL, erR;
    std::array<float, kErTaps> erTimeL {}, erTimeR {}, erGain {};
    float erOut = 0.3f;

    // nonlin
    std::array<float, kNlTaps> nlTime {}, nlGain {};

    // tank
    std::array<DelayLine, N> line;
    std::array<Allpass, N> lineAp;
    std::array<Smoother, N> lenSm;
    std::array<float, N> baseLen {}, gLow {}, gHigh {}, fb {};
    std::array<OnePole, N> xover, bwLoop;
    std::array<Svf, N> hfDamp;          // 12 dB/oct split for the HighShelf damping (a 6 dB/oct split dulled 2-4 kHz too)
    bool loopBwOn = true;               // 1970s / 1980s colours only: NOW keeps the loop full bandwidth
    std::array<float, N> lfoPhase {}, lfoInc {};
    std::array<RandomLfo, N> rnd;
    std::array<float, N> walk {};
    float modSamples = 0.0f, chorusOut = 0.0f;
    std::array<float, N> hsGain {};   // extra per-pass gain above highFreq (from the HighShelf RT ratio)
    float drive = 0.0f;
    Rng noise { 99 };

    // output
    Svf hcL, hcR, lcL, lcR;
    OnePole outBwL, outBwR;
    float quantStep = 0.0f;
    float widthSm = 1.0f;
    float envOut = 0.0f;
    float outGain = 3.0f;
};
} // namespace si::dsp
