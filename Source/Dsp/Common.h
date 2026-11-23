#pragma once
// Stratum - shared constants and small helpers (no JUCE dependency).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace stratum
{
constexpr int kLayers = 8;
constexpr int kOscs = 3;
constexpr int kEnvs = 4;
constexpr int kLfos = 4;
constexpr int kSeqs = 4;
constexpr int kModSlots = 8;
constexpr int kFxSlots = 7;
constexpr int kMaxUnison = 8;
constexpr int kMaxSteps = 32;
constexpr int kArpSteps = 16;
constexpr int kCtlRate = 16;   // samples per control-rate chunk
constexpr int kMaxVoices = 16; // per layer

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float midiToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

// Cheap deterministic noise source, returns -1..1
struct Rng
{
    uint32_t s = 0x1234567u;
    float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f; }
    float next01() { return next() * 0.5f + 0.5f; }
};

// Tempo-sync divisions, expressed as cycles per beat (quarter note).
constexpr int kNumDivs = 14;
// Entries 0-8 are straight divisions; 9-13 are dotted (1.5x as long, so 2/3 of the cycles per beat).
// Dotted entries are appended so parameter values saved by earlier versions keep their meaning.
constexpr float kDivCyclesPerBeat[kNumDivs] = { 0.0625f, 0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f,
                                                0.33333334f, 0.6666667f, 1.3333334f, 2.6666667f, 5.3333335f };
inline const char* divName (int i)
{
    static const char* n[kNumDivs] = { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
                                        "1/2 dotted", "1/4 dotted", "1/8 dotted", "1/16 dotted", "1/32 dotted" };
    return n[std::min (std::max (i, 0), kNumDivs - 1)];
}

struct Transport
{
    double bpm = 120.0;
    double ppq = 0.0;       // quarter-note position at start of the block
    bool playing = false;
};
} // namespace stratum
