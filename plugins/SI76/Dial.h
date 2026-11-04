#pragma once
#include <juce_core/juce_core.h>

/** 1176-style attenuator dial: the knob position (0..1) maps to the printed scale
    "inf 48 36 30 24 18 12 6 0" (dB of attenuation, evenly spaced around the dial). */
namespace si76
{
constexpr int kDialMarks = 9;
inline const float* dialPos() { static const float p[kDialMarks] = { 0.0f, 0.125f, 0.25f, 0.375f, 0.5f, 0.625f, 0.75f, 0.875f, 1.0f }; return p; }
inline const float* dialAtt() { static const float a[kDialMarks] = { 100.0f, 48.0f, 36.0f, 30.0f, 24.0f, 18.0f, 12.0f, 6.0f, 0.0f }; return a; }   // 100 = "infinity"

inline float posToAtt (float p)
{
    p = juce::jlimit (0.0f, 1.0f, p);
    for (int i = 0; i < kDialMarks - 1; ++i)
        if (p <= dialPos()[i + 1])
        {
            const float t = (p - dialPos()[i]) / (dialPos()[i + 1] - dialPos()[i]);
            return dialAtt()[i] + (dialAtt()[i + 1] - dialAtt()[i]) * t;
        }
    return 0.0f;
}

inline float attToPos (float a)
{
    a = juce::jlimit (0.0f, 100.0f, a);
    for (int i = 0; i < kDialMarks - 1; ++i)
        if (a >= dialAtt()[i + 1])
        {
            const float t = (dialAtt()[i] - a) / (dialAtt()[i] - dialAtt()[i + 1]);
            return dialPos()[i] + (dialPos()[i + 1] - dialPos()[i]) * t;
        }
    return 1.0f;
}

inline juce::String infinity() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")); }
inline juce::String dialLabel (int i) { return i == 0 ? infinity() : juce::String ((int) dialAtt()[i]); }
inline juce::String attText (float p) { return p < 0.02f ? infinity() : juce::String (juce::roundToInt (posToAtt (p))); }

// the gain each scale position produces: Input drives into the fixed threshold, Output is make-up
constexpr float kInputMax = 48.0f;   // input fully clockwise ("0") = +48 dB of drive
constexpr float kOutputMax = 12.0f;  // output at "0" = +12 dB, at "12" = unity
inline float inputDb (float p) { return kInputMax - posToAtt (p); }
inline float outputDb (float p) { return kOutputMax - posToAtt (p); }
inline float inputPosForDb (float db) { return attToPos (kInputMax - db); }
inline float outputPosForDb (float db) { return attToPos (kOutputMax - db); }
} // namespace si76
