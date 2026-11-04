#pragma once
#include <vector>
#include <cmath>
#include <functional>

/** Offline note-based pitch editing (Melodyne-style):
    analyse() tracks the pitch of a monophonic take, splits it into notes and places pitch marks;
    render() moves each note's pitch centre while keeping the singer's own drift / vibrato / slides,
    using TD-PSOLA so the formants (the "voice") stay where they are. */
namespace si::pitch
{
struct Note
{
    int start = 0, end = 0;        // samples in the take
    float center = 0.0f;           // measured pitch centre (MIDI, fractional)
    bool manual = false;           // moved by hand: `target` is used as is
    float target = 0.0f;           // MIDI, only when manual
    float sung = 1.0f;             // 0..1: held, stable note (1) vs spoken / gliding syllable (0); scales auto correction
};

struct Analysis
{
    double sr = 48000.0;
    int hop = 240;                         // samples per frame (5 ms)
    std::vector<float> midi;               // per frame, <= 0 = unvoiced
    std::vector<float> smooth;             // slow pitch (drift), per frame (copy of midi where unvoiced)
    std::vector<float> rms;                // per frame, linear
    float peakRms = 0.0f;
    std::vector<Note> notes;
    std::vector<int> marks;                // pitch marks (sample positions), sorted
    std::vector<char> markVoiced;
    int length = 0;                        // samples
    int frameOf (int sample) const { return std::min ((int) midi.size() - 1, std::max (0, sample / hop)); }
};

struct Correction
{
    int scaleMask = 0xFFF;    // bit n = pitch class n allowed (C = 0)
    float center = 0.0f;      // 0..1: how far auto notes move to the nearest scale note
    float drift = 0.0f;       // 0..1: how much of the slow pitch drift inside a note is flattened
    float vibrato = 1.0f;     // 0..2: scales the singer's vibrato (1 = untouched)
};

/** Mono analysis. `progress` (optional) is called with 0..1. */
Analysis analyse (const float* mono, int n, double sr, const std::function<void (float)>& progress = {});

float nearestInScale (float midi, int scaleMask);
/** Where a note's centre ends up (MIDI). */
float correctedCenter (const Note&, const Correction&);
/** Per-frame pitch offset in semitones (0 = untouched). */
std::vector<float> offsetCurve (const Analysis&, const std::vector<Note>&, const Correction&);
bool isIdentity (const std::vector<float>& offset);

/** Renders `numCh` channels of `in` (each a.length samples) into `out` with the given per-frame offsets. */
void render (const Analysis&, const std::vector<float>& offset, const float* const* in, float* const* out, int numCh);
} // namespace si::pitch
