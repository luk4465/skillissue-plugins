#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

/** FET compressor engine (1176-style), matched to measurements of a reference emulation: measured Input / Output
    knob laws, table-driven static curves per ratio and model, peak detection with two-stage release, very low
    harmonics at Color 35 %, parallel mix. */
struct FetComp
{
    void prepare (double sampleRate);
    void reset();
    // returns gain reduction in dB (<= 0) of the last block
    float process (float* l, float* r, int n, int channels);

    // inputDb: 0..48 = dial 48..0 (the SI 76 mapping), outputDb: +12..-36 = dial 0..48
    float inputDb = 18.0f, outputDb = 0.0f, attackKnob = 3.0f, releaseKnob = 5.0f, mix = 1.0f, color = 0.35f;
    int ratio = 0;          // 0=4, 1=8, 2=12, 3=20, 4=All
    float scHpf = 0.0f;     // Hz, 0 = off
    bool link = true;
    int model = 0;          // 0 = Blue (smooth classic), 1 = Black (aggressive, rap)
    const float* keyL = nullptr;   // external sidechain for the detector (nullptr = the input itself)
    const float* keyR = nullptr;

    double sr = 48000.0;
    float hpZ[2] {}, grFast[2] {}, grSlow[2] {}, grSm = 0.0f, pkHold[2] {};
    int pkCnt[2] {};
    std::array<std::array<float, 5>, 2> pkC {}, lpC {}, lsC {}, hiC {};   // response filters per model
    float makeupDb[2] {};
    float pkZ[2][2] {}, lpZ[2][2] {}, lsZ[2][2] {}, lfZ[2] {}, lfA = 0.0f, evZ[2] {}, evA = 0.0f, hiZ[2][2] {};
    float inPeak = 0.0f, outPeak = 0.0f;
};
