#pragma once
#include <juce_core/juce_core.h>

namespace si::tempo
{
/** Musical note values, length in quarter-note beats. */
struct Note { const char* name; double beats; };

struct NoteList
{
    NoteList (std::initializer_list<Note> l) : v (l) {}
    std::vector<Note> v;
    int size() const { return (int) v.size(); }
    const Note& operator[] (int i) const { return v[(size_t) i]; }
    auto begin() const { return v.begin(); }
    auto end() const { return v.end(); }
};

// Pre-delay grid (index 0 = Off)
inline const NoteList& preDelayNotes()
{
    static const NoteList n {
        { "Off", 0.0 }, { "1/128", 1.0 / 32 }, { "1/64T", 1.0 / 24 }, { "1/64", 1.0 / 16 }, { "1/64D", 3.0 / 32 },
        { "1/32T", 1.0 / 12 }, { "1/32", 1.0 / 8 }, { "1/32D", 3.0 / 16 }, { "1/16T", 1.0 / 6 }, { "1/16", 0.25 },
        { "1/16D", 0.375 }, { "1/8T", 1.0 / 3 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3 },
        { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2", 2.0 }
    };
    return n;
}

// Decay grid (index 0 = Off)
inline const NoteList& decayNotes()
{
    static const NoteList n {
        { "Off", 0.0 }, { "1/8", 0.5 }, { "1/4", 1.0 }, { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 },
        { "1 Bar", 4.0 }, { "1.5 Bar", 6.0 }, { "2 Bars", 8.0 }, { "3 Bars", 12.0 }, { "4 Bars", 16.0 },
        { "8 Bars", 32.0 }, { "16 Bars", 64.0 }
    };
    return n;
}

// Rows of the tempo table
inline const NoteList& tableNotes()
{
    static const NoteList n {
        { "1/1", 4.0 }, { "1/2", 2.0 }, { "1/4", 1.0 }, { "1/8", 0.5 }, { "1/16", 0.25 }, { "1/32", 0.125 }, { "1/64", 0.0625 }
    };
    return n;
}

inline double beatsToMs (double beats, double bpm) { return beats * 60000.0 / juce::jmax (1.0, bpm); }

inline juce::StringArray names (const NoteList& notes)
{
    juce::StringArray s;
    for (auto& n : notes) s.add (n.name);
    return s;
}

/** Index of the grid value whose time is closest to targetMs (skips "Off"). */
inline int closest (const NoteList& notes, double targetMs, double bpm)
{
    int best = 1;
    double bestErr = 1e9;
    for (int i = 1; i < notes.size(); ++i)
    {
        // compare in log domain: musical ratios matter, not absolute ms
        const double err = std::abs (std::log (beatsToMs (notes[i].beats, bpm) / juce::jmax (0.1, targetMs)));
        if (err < bestErr) { bestErr = err; best = i; }
    }
    return best;
}

inline juce::String formatMs (double ms)
{
    if (ms >= 1000.0) return juce::String (ms / 1000.0, ms >= 10000.0 ? 1 : 2) + " s";
    return (ms < 10.0 ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms))) + " ms";
}
} // namespace si::tempo
