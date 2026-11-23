#pragma once
// Stratum - envelopes, LFOs and step sequencers (no JUCE dependency).
#include "Common.h"

namespace stratum
{
//==============================================================================
struct EnvParams
{
    float velocity = 0.0f;   // how much key velocity scales the output 0..1
    float delay = 0.0f;      // seconds
    float attack = 0.005f;   // seconds
    float level = 1.0f;      // level at the start of decay (not forced to be the peak)
    float decay = 0.3f;      // seconds
    float sustain = 0.7f;
    float release = 0.3f;    // seconds
    float slopeA = 0.5f, slopeD = 0.5f, slopeR = 0.5f; // 0..1, higher = faster initial movement
};

struct Envelope
{
    enum Stage { Idle, Delay, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float pos = 0.0f;
    float value = 0.0f;
    float releaseFrom = 0.0f;
    float vel = 1.0f;

    static float shape (float x, float slope) { return std::pow (clampf (x, 0.0f, 1.0f), std::pow (4.0f, 0.5f - slope)); }

    void noteOn (float velocity) { stage = Delay; pos = 0.0f; vel = velocity; }
    void noteOff()
    {
        if (stage != Idle && stage != Release) { releaseFrom = value; stage = Release; pos = 0.0f; }
    }
    void kill() { stage = Idle; value = 0.0f; pos = 0.0f; }
    bool active() const { return stage != Idle; }

    float advance (float dt, const EnvParams& p)
    {
        float velGain = lerpf (1.0f, vel, p.velocity);
        switch (stage)
        {
            case Idle: value = 0.0f; break;
            case Delay:
                pos += dt;
                value = 0.0f;
                if (pos >= p.delay) { stage = Attack; pos = 0.0f; }
                break;
            case Attack:
            {
                pos += dt;
                float x = p.attack <= 1.0e-4f ? 1.0f : pos / p.attack;
                value = p.level * shape (x, p.slopeA);
                if (x >= 1.0f) { stage = Decay; pos = 0.0f; value = p.level; }
                break;
            }
            case Decay:
            {
                pos += dt;
                float x = p.decay <= 1.0e-4f ? 1.0f : pos / p.decay;
                value = lerpf (p.level, p.sustain, shape (x, p.slopeD));
                if (x >= 1.0f) { stage = Sustain; value = p.sustain; }
                break;
            }
            case Sustain: value = p.sustain; break;
            case Release:
            {
                pos += dt;
                float x = p.release <= 1.0e-4f ? 1.0f : pos / p.release;
                value = releaseFrom * (1.0f - shape (x, p.slopeR));
                if (x >= 1.0f) { stage = Idle; value = 0.0f; }
                break;
            }
        }
        return value * velGain;
    }
};

//==============================================================================
struct LfoParams
{
    int waveA = 0, waveB = 0;   // 0 sine 1 tri 2 saw up 3 saw down 4 square 5 sample&hold 6 glide random
    float xfade = 0.0f;         // 0..1 between A and B
    float phase = 0.0f;         // 0..1
    float bend = 0.0f;          // -1..1
    float rateHz = 1.0f;
    float level = 1.0f;
    float delay = 0.0f, fade = 0.0f;
    bool sync = false;
    int syncDiv = 4;
    bool retrigger = true;
    bool bipolar = true;
};

struct Lfo
{
    float phase = 0.0f, t = 0.0f;
    float rA = 0.0f, rB = 0.0f; // random values (previous / next)
    Rng rng;
    float value = 0.0f;

    void reset (uint32_t seed)
    {
        phase = 0.0f; t = 0.0f; rng.s = seed * 2654435761u + 12345u;
        rA = rng.next(); rB = rng.next();
    }

    static float wave (int w, float ph, float rPrev, float rNext)
    {
        switch (w)
        {
            case 0: return std::sin (kTwoPi * ph);
            case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case 2: return 2.0f * ph - 1.0f;
            case 3: return 1.0f - 2.0f * ph;
            case 4: return ph < 0.5f ? 1.0f : -1.0f;
            case 5: return rNext;
            default: { float s = ph * ph * (3.0f - 2.0f * ph); return lerpf (rPrev, rNext, s); }
        }
    }

    float advance (float dt, const LfoParams& p, double bpm, float freeStartPhase = 0.0f)
    {
        t += dt;
        float rate = p.sync ? (float) (bpm / 60.0) * kDivCyclesPerBeat[(int) clampf ((float) p.syncDiv, 0, kNumDivs - 1)] : p.rateHz;
        phase += rate * dt;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            rA = rB; rB = rng.next();
        }
        float ph = std::fmod (phase + p.phase + freeStartPhase, 1.0f);
        ph = std::pow (ph, std::pow (2.0f, p.bend * 2.0f));
        float a = wave (p.waveA, ph, rA, rB);
        float v = a;
        if (p.xfade > 0.001f) v = lerpf (a, wave (p.waveB, ph, rA, rB), p.xfade);
        if (! p.bipolar) v = v * 0.5f + 0.5f;
        float g = 1.0f;
        if (t < p.delay) g = 0.0f;
        else if (p.fade > 1.0e-4f) g = clampf ((t - p.delay) / p.fade, 0.0f, 1.0f);
        value = v * p.level * g;
        return value;
    }
};

//==============================================================================
struct SeqParams
{
    int length = 16;
    int loopStart = 0;
    float rateHz = 4.0f;
    bool sync = true;
    int syncDiv = 6;
    bool oneshot = false;
    bool retrigger = true;
    float level = 1.0f;
    float smooth = 0.0f;
    float shuffle = 0.0f;
    float steps[kMaxSteps] = {};   // -1..1
};

struct StepSeq
{
    float pos = 0.0f;
    float value = 0.0f;
    bool finished = false;

    void reset() { pos = 0.0f; value = 0.0f; finished = false; }

    float advance (float dt, const SeqParams& p, double bpm)
    {
        float stepsPerSec = p.sync ? (float) (bpm / 60.0) * kDivCyclesPerBeat[(int) clampf ((float) p.syncDiv, 0, kNumDivs - 1)] : p.rateHz;
        int len = std::max (1, std::min (p.length, kMaxSteps));
        int loop = std::min (std::max (p.loopStart, 0), len - 1);
        if (! finished) pos += stepsPerSec * dt;
        int idx = (int) pos;
        if (idx >= len)
        {
            if (p.oneshot) { idx = len - 1; finished = true; pos = (float) idx + 0.999f; }
            else
            {
                int span = len - loop;
                idx = loop + ((idx - loop) % span);
                pos = (float) idx + (pos - std::floor (pos));
            }
        }
        float target = p.steps[idx];
        float coef = p.smooth <= 0.001f ? 1.0f : 1.0f - std::exp (-dt * (200.0f * (1.0f - p.smooth) + 2.0f));
        value += (target - value) * coef;
        return value * p.level;
    }
};
} // namespace stratum
