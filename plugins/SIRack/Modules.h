#pragma once
#include <array>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>

namespace si::rack
{
constexpr int kSlots = 8;
constexpr int kParams = 6;

enum Module { Empty = 0, EQ73, EQP1A, FET76, Opto2A, BusVCA, Console, Exciter, Tape, DeEsser, Gate, Transient, Limiter, Width, kNumModules };

/** One knob of a module: real range, curve, unit and optional stepped choices. */
struct KnobDef
{
    const char* name = nullptr;      // nullptr = unused slot
    float lo = 0, hi = 1, def = 0;
    bool log = false;
    const char* unit = "";
    std::vector<const char*> steps;   // stepped (hardware switch) values shown by name
    std::vector<float> stepValues;
};

struct ModuleDef
{
    const char* name;
    const char* category;            // EQ, Dynamics, Color, Utility
    const char* blurb;
    std::array<KnobDef, kParams> knobs;
    std::vector<const char*> modes;  // optional switch
    bool hasGR = false;
    juce::uint32 tint;               // faceplate accent
};

const ModuleDef& moduleDef (int type);
float knobReal (int type, int idx, float norm);       // 0..1 -> real value
float knobNorm (int type, int idx, float real);       // real -> 0..1
float knobDefaultNorm (int type, int idx);
juce::String knobText (int type, int idx, float norm);

/** A module instance; process() receives normalised knob values. */
class Processor
{
public:
    virtual ~Processor() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    virtual void process (float* l, float* r, int n, const std::array<float, kParams>& knob, int mode) = 0;
    float grDb = 0.0f;               // last block's gain reduction (dynamics modules)
    const float* keyL = nullptr;     // external sidechain for the detector (FET, Opto, Bus, Gate); nullptr = own input
    const float* keyR = nullptr;
};

std::unique_ptr<Processor> createProcessor (int type);
} // namespace si::rack
