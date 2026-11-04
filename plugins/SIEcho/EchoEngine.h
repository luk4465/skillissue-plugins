#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace si::echo
{
constexpr int kNumStyles = 16;
constexpr int kNumPatterns = 8;
const char* styleName (int s);
const char* patternName (int p);
/** Tap levels (0..1) of a rhythm pattern, 8 steps. */
const std::array<float, 8>& pattern (int p);

enum class Mode { Single = 0, Dual, PingPong, Rhythm };

struct EchoParams
{
    Mode mode = Mode::Single;
    double timeL = 0.5, timeR = 0.5;   // seconds
    float feedback = 0.35f;            // 0..1.1
    float mix = 0.3f;
    float drive = 0.2f;                // 0..1 extra saturation in the loop
    float lowCut = 80.0f, highCut = 8000.0f;
    int style = 1;
    float wobble = 0.0f;               // 0..1 added wow / flutter
    float diffusion = 0.0f;            // 0..1
    float width = 1.0f;                // 0..1.5
    float ducking = 0.0f;              // 0..1
    float groove = 0.0f;               // -1..1 shifts the right / every 2nd tap
    int patternIdx = 0;
    bool freeze = false;
    // H-Delay style controls
    int analog = 0;                    // 0 off (uses the Character style), 1..4 analog models
    bool lofi = false;
    float modDepth = 0.0f, modRate = 1.0f;   // 0..1, Hz
    bool phaseL = false, phaseR = false;
    float outputDb = 0.0f;
};

/** EchoBoy-style echo: stereo delay lines with tape / BBD / lo-fi characters inside the feedback loop. */
class EchoEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const EchoParams& p) { prm = p; }
    void process (float* l, float* r, int n);
    std::atomic<float> wetLevel { 0.0f }, inLevel { 0.0f };
    const float* keyL = nullptr;   // external sidechain for Ducking (nullptr = the input itself)
    const float* keyR = nullptr;

private:
    struct Line
    {
        std::vector<float> buf;
        int w = 0;
        void write (float x) { buf[(size_t) w] = x; w = (w + 1) % (int) buf.size(); }
        float read (double delaySamples) const;
    };
    struct Ap { std::vector<float> b; int w = 0; float process (float x, int d, float g); };
    float loop (int c, float x);   // character processing inside the feedback path

    double sr = 48000.0;
    EchoParams prm;
    Line line[2];
    std::array<std::array<Ap, 3>, 2> ap;
    // 12 dB/oct TPT state-variable filters (HiPass / LoPass) on everything written into the lines
    float hp1[2] {}, hp2[2] {}, lp1[2] {}, lp2[2] {}, held[2] {};
    float lq1[2] {}, lq2[2] {};
    float hpA1 = 1.0f, hpA2 = 0.0f, hpA3 = 0.0f, lpA1 = 1.0f, lpA2 = 0.0f, lpA3 = 0.0f, lqA1 = 1.0f, lqA2 = 0.0f, lqA3 = 0.0f;
    static constexpr float kHpK = 1.41421356f;   // HiPass damping (Butterworth)
    void updateFilters();
    int holdCount = 0;
    double wowPh = 0.0, flutPh = 0.0, chorPh = 0.0, modPh = 0.0;
    int styleNow() const;
    float drift = 0.0f, driftT = 0.0f, duckEnv = 0.0f, envIn = 0.0f, wetEnv = 0.0f;
    double smT[2] { 0.5, 0.5 };
    unsigned seed = 12345u;
    float rnd() { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) & 0xFFFF) / 32767.5f - 1.0f; }
};
} // namespace si::echo
