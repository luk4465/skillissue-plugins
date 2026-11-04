#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace si
{
struct RiserSettings
{
    double bpm = 120.0;
    double lengthBeats = 4.0;
    int feed = 0;              // 0 = auto (first phrase), 1..5 = note grid, 6 = all
    int verbMode = 0, verbColor = 2;
    float size = 0.7f, tail = 1.5f, hiCut = 12000.0f, loCut = 120.0f;
    float deEss = 0.6f;
    float shape = 0.0f;        // -1..1
    float pitch = 0.0f;        // semitones at riser start (<= 0)
    float filter = 0.0f;       // 0..1
    int filterType = 0;        // 0 LP open, 1 HP fall, 2 BP sweep
    float widthStart = 0.3f, widthEnd = 1.2f;
    bool phone = false, chorus = false, tremolo = false, stutter = false, lofi = false, echo = false;
    double fxBeats = 0.25;
    bool accel = true;
    bool includeDry = true, normalize = true;
    float gainDb = 0.0f;
    float post = 0.0f;         // forward reverb on the dry part after the drop, 0..1
};

struct RiserResult
{
    juce::AudioBuffer<float> audio;
    int riserSamples = 0;      // drop point (where the dry source starts)
    double sampleRate = 48000.0;
    double bpm = 120.0, lengthBeats = 4.0;
};

/** Offline reverse-reverb riser: source -> de-ess -> reverse -> (echo) -> reverb -> reverse -> shape/FX. */
std::unique_ptr<RiserResult> renderRiser (const RiserSettings&, const juce::AudioBuffer<float>& source, double sampleRate,
                                          const std::atomic<bool>* cancel = nullptr);

/** First sample above -30 dB relative to the peak, or -1 if silent. */
int findOnset (const juce::AudioBuffer<float>&);
} // namespace si
