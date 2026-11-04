#pragma once
#include <array>
#include <atomic>
#include <cmath>
#include <complex>

namespace si::eq
{
constexpr int kBands = 8;
enum class Type { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, Tilt };
enum class Chan { Stereo = 0, Mid, Side, Left, Right };

struct BandParams
{
    bool on = false;
    Type type = Type::Bell;
    float freq = 1000.0f, gain = 0.0f, q = 0.71f;
    int slope = 1;            // cuts: 0=12, 1=24, 2=36, 3=48 dB/oct
    Chan chan = Chan::Stereo;
    bool dyn = false;
    float thresh = -24.0f;    // dB
    float target = 0.0f;      // dB gain reached when far above threshold
    bool tight = true;        // Tight = fast release, Smooth = slow
};

/** One 2nd-order TPT state-variable section (Cytomic form): out = m0*x + m1*band + m2*low. */
struct Section
{
    float g = 0, k = 1.414f, m0 = 1, m1 = 0, m2 = 0, a1 = 1, a2 = 0, a3 = 0;
    float ic1[2] {}, ic2[2] {};
    void set (float gIn, float kIn, float m0In, float m1In, float m2In);
    inline float tick (float x, int ch)
    {
        const float v3 = x - ic2[ch];
        const float v1 = a1 * ic1[ch] + a2 * v3;
        const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch];
        ic2[ch] = 2.0f * v2 - ic2[ch];
        return m0 * x + m1 * v1 + m2 * v2;
    }
    /** Exact magnitude of this section at frequency f (bilinear mapping, same prewarp). */
    float magnitude (float f, float sr) const;
    void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
};

/** Design the sections of one band; returns how many are used (max 4). */
int designBand (const BandParams&, float gainDb, float sr, std::array<Section, 4>& out);
/** Magnitude in dB of one band at frequency f, with the given gain (static or live). Thread-safe (no state). */
float bandResponseDb (const BandParams&, float gainDb, float f, float sr);

class EqEngine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setBand (int i, const BandParams& p);
    void setSolo (int band) { solo = band; }       // -1 = off
    void process (float* l, float* r, int n);
    const float* keyL = nullptr;   // external sidechain for the dynamic bands (nullptr = the input itself)
    const float* keyR = nullptr;

    float getLiveGain (int i) const { return liveGain[(size_t) i].load(); }

private:
    struct Band
    {
        BandParams p;
        std::array<Section, 4> sec;
        int nSec = 0;
        Section side;        // band-pass detector for dynamics
        float env = 0.0f, curGain = 0.0f;
        bool dirty = true;
    };
    void design (Band&, float gainDb);
    float sr = 48000.0f;
    std::array<Band, kBands> bands;
    std::array<std::atomic<float>, kBands> liveGain {};
    Section soloBp;
    int solo = -1, soloDesigned = -2;
    float soloFreq = 0, soloQ = 0;
};
} // namespace si::eq
