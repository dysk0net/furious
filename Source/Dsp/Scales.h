#pragma once
// Stratum - musical scales for the sequencers and the arpeggiator (no JUCE dependency).
#include "Common.h"

namespace stratum
{
constexpr int kNumScales = 10;

struct ScaleDef
{
    const char* name;
    int count;          // notes per octave
    int degrees[8];     // semitones above the key
};

inline const ScaleDef& scaleDef (int i)
{
    static const ScaleDef defs[kNumScales] = {
        { "No scale",           12, { 0 } },                          // chromatic: nothing is snapped
        { "Major",               7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",               7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Phrygian",            7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Blues",               6, { 0, 3, 5, 6, 7, 10 } },
        { "Phrygian Dominant",   7, { 0, 1, 4, 5, 7, 8, 10 } },
        { "Double Harmonic",     7, { 0, 1, 4, 5, 7, 8, 11 } },
        { "Hungarian Minor",     7, { 0, 2, 3, 6, 7, 8, 11 } },
        { "Hirajoshi",           5, { 0, 2, 3, 7, 8 } },
        { "Whole Tone",          6, { 0, 2, 4, 6, 8, 10 } },
    };
    return defs[std::min (std::max (i, 0), kNumScales - 1)];
}

inline const char* scaleName (int i) { return scaleDef (i).name; }

inline const char* keyName (int i)
{
    static const char* n[12] = { "Key C", "Key C#", "Key D", "Key D#", "Key E", "Key F",
                                 "Key F#", "Key G", "Key G#", "Key A", "Key A#", "Key B" };
    return n[std::min (std::max (i, 0), 11)];
}

// Snaps a note number (MIDI numbering, fractions allowed) to the nearest note of the scale in the given key
// (0 = C ... 11 = B). A note exactly between two scale notes goes to the lower one. Scale 0 changes nothing.
inline float quantizeToScale (float note, int scale, int key)
{
    if (scale <= 0) return note;
    const ScaleDef& s = scaleDef (scale);
    const float rel = note - (float) key;
    const float oct = std::floor (rel / 12.0f);
    const float pc = rel - oct * 12.0f;                       // 0 .. 12 above the key
    float best = (float) s.degrees[0], bestDist = 1.0e9f;
    for (int i = 0; i <= s.count; ++i)                        // the extra entry is the octave above the key
    {
        const float d = i < s.count ? (float) s.degrees[i] : 12.0f;
        const float dist = std::fabs (pc - d);
        if (dist < bestDist - 1.0e-4f) { bestDist = dist; best = d; }
    }
    return (float) key + oct * 12.0f + best;
}
} // namespace stratum
