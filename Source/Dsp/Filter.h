#pragma once
// Stratum - per-voice stereo filter and insert effect (no JUCE dependency).
#include "Common.h"

namespace stratum
{
//==============================================================================
enum FilterType
{
    FltOff = 0, FltLP12, FltLP24, FltHP12, FltHP24, FltBP12, FltNotch, FltPeak,
    FltLP6, FltHP6, FltLadder, FltComb, FltFormant, FltNumTypes
};

inline const char* filterTypeName (int t)
{
    static const char* n[FltNumTypes] = { "Off", "LP-12", "LP-24", "HP-12", "HP-24", "BP-12", "Notch",
                                          "Peak", "LP-6", "HP-6", "Ladder", "Comb", "Formant" };
    return n[std::min (std::max (t, 0), (int) FltNumTypes - 1)];
}

struct FilterParams
{
    int type = FltLP24;
    float cutoff = 0.7f;      // 0..1 (20 Hz .. 22 kHz, exponential)
    float resonance = 0.2f;   // 0..1
    float treble = 0.0f;      // -1..1 brightness tilt
    float envAmount = 0.0f;   // -1..1 (envelope B)
    float keyFollow = 0.0f;   // -1..1
    float width = 0.5f;       // bandwidth / vowel / comb damping
};

inline float cutoffToHz (float c) { return 20.0f * std::pow (1100.0f, clampf (c, 0.0f, 1.0f)); }

struct Svf
{
    float s1 = 0.0f, s2 = 0.0f;
    inline void tick (float x, float g, float r2, float& lp, float& bp, float& hp)
    {
        hp = (x - (r2 + g) * s1 - s2) / (1.0f + r2 * g + g * g);
        bp = g * hp + s1;  s1 = g * hp + bp;
        lp = g * bp + s2;  s2 = g * bp + lp;
    }
    void reset() { s1 = s2 = 0.0f; }
};

class StereoFilter
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        reset();
    }
    void reset()
    {
        for (auto& c : ch) { c.a.reset(); c.b.reset(); c.c.reset(); c.d.reset(); c.one = 0; c.comb.fill (0.0f); c.combPos = 0; c.ladder[0] = c.ladder[1] = c.ladder[2] = c.ladder[3] = 0; }
    }

    // Set coefficients from a cutoff frequency in Hz and the parameter block.
    void setup (const FilterParams& p, float cutoffHz)
    {
        type = p.type;
        float fc = clampf (cutoffHz, 16.0f, 0.46f * fs);
        g = std::tan (kPi * fc / fs);
        float res = clampf (p.resonance, 0.0f, 1.0f);
        r2 = 2.0f - 1.96f * res;                  // 1/Q, 2.0 .. 0.04
        r2b = clampf (r2 * (0.4f + p.width * 1.6f), 0.04f, 2.0f);   // width widens band/notch/peak
        treble = p.treble;
        combDelay = clampf (fs / fc, 2.0f, (float) kCombMax - 2.0f);
        combFb = clampf (0.2f + 0.78f * res, 0.0f, 0.97f);
        combDamp = clampf (p.width, 0.0f, 1.0f);
        vowel = clampf (p.width, 0.0f, 1.0f);
        ladK = 4.0f * res * 0.95f;
        onePoleG = g / (1.0f + g);
    }

    inline void process (float& l, float& r)
    {
        if (type == FltOff) return;
        l = run (ch[0], l);
        r = run (ch[1], r);
    }

private:
    static constexpr int kCombMax = 2048;
    struct Chan
    {
        Svf a, b, c, d;
        float one = 0.0f;
        float ladder[4] = {};
        std::array<float, kCombMax> comb {};
        int combPos = 0;
        float combLp = 0.0f;
        float trebleLp = 0.0f;
    };
    Chan ch[2];
    float fs = 44100.0f, g = 0.1f, r2 = 1.0f, r2b = 1.0f, treble = 0.0f;
    float combDelay = 100.0f, combFb = 0.5f, combDamp = 0.5f, vowel = 0.0f, ladK = 0.0f, onePoleG = 0.1f;
    int type = FltLP24;

    inline float run (Chan& c, float x)
    {
        float lp, bp, hp, out = x;
        switch (type)
        {
            case FltLP12: c.a.tick (x, g, r2, lp, bp, hp); out = lp; break;
            case FltLP24: c.a.tick (x, g, r2, lp, bp, hp); c.b.tick (lp, g, r2, lp, bp, hp); out = lp; break;
            case FltHP12: c.a.tick (x, g, r2, lp, bp, hp); out = hp; break;
            case FltHP24: c.a.tick (x, g, r2, lp, bp, hp); c.b.tick (hp, g, r2, lp, bp, hp); out = hp; break;
            case FltBP12: c.a.tick (x, g, r2b, lp, bp, hp); out = bp * r2b; break;
            case FltNotch: c.a.tick (x, g, r2b, lp, bp, hp); out = x - bp * r2b; break;
            case FltPeak: c.a.tick (x, g, r2b, lp, bp, hp); out = x + bp * (2.0f - r2b) * 2.0f; break;
            case FltLP6: c.one += onePoleG * (x - c.one); out = c.one; break;
            case FltHP6: c.one += onePoleG * (x - c.one); out = x - c.one; break;
            case FltLadder:
            {
                float in = std::tanh (x - ladK * c.ladder[3]);
                float gg = onePoleG;
                c.ladder[0] += gg * (in - c.ladder[0]);
                c.ladder[1] += gg * (c.ladder[0] - c.ladder[1]);
                c.ladder[2] += gg * (c.ladder[1] - c.ladder[2]);
                c.ladder[3] += gg * (c.ladder[2] - c.ladder[3]);
                out = c.ladder[3] * (1.0f + ladK * 0.25f);
                break;
            }
            case FltComb:
            {
                int d = (int) combDelay;
                float frac = combDelay - (float) d;
                int i0 = (c.combPos - d + kCombMax) & (kCombMax - 1);
                int i1 = (i0 - 1 + kCombMax) & (kCombMax - 1);
                float del = c.comb[(size_t) i0] * (1.0f - frac) + c.comb[(size_t) i1] * frac;
                c.combLp += (1.0f - combDamp * 0.9f) * (del - c.combLp);
                float w = x + c.combLp * combFb;
                c.comb[(size_t) c.combPos] = std::tanh (w);
                c.combPos = (c.combPos + 1) & (kCombMax - 1);
                out = w * 0.6f;
                break;
            }
            case FltFormant:
            {
                // two band-passes sweeping between vowel formant pairs (a, e, i, o, u)
                static const float f1[5] = { 800, 400, 270, 450, 325 };
                static const float f2[5] = { 1150, 2000, 2140, 800, 700 };
                float pos = vowel * 4.0f;
                int i0 = std::min ((int) pos, 3);
                float fr = pos - (float) i0;
                float a = lerpf (f1[i0], f1[i0 + 1], fr), b = lerpf (f2[i0], f2[i0 + 1], fr);
                float ga = std::tan (kPi * a / fs), gb = std::tan (kPi * b / fs);
                float l1, b1, h1, l2, b2, h2;
                c.a.tick (x, ga, 0.25f, l1, b1, h1);
                c.b.tick (x, gb, 0.25f, l2, b2, h2);
                out = (b1 + 0.7f * b2) * 0.5f;
                break;
            }
            default: break;
        }
        if (treble != 0.0f)
        {
            c.trebleLp += 0.25f * (out - c.trebleLp);
            out += (out - c.trebleLp) * treble * 1.5f;
        }
        return out;
    }
};

//==============================================================================
enum InsertType
{
    InsOff = 0, InsTube, InsOverdrive, InsToneFuzz, InsBitcrush, InsExciter, InsRingMod, InsNumTypes
};

inline const char* insertTypeName (int t)
{
    static const char* n[InsNumTypes] = { "Off", "Tube Distortion", "Overdrive", "Tone Fuzz", "Bitcrusher", "Exciter", "Ring Mod" };
    return n[std::min (std::max (t, 0), (int) InsNumTypes - 1)];
}

struct InsertParams
{
    int type = InsOff;
    float drive = 0.3f;
    float p2 = 0.5f;
    float p3 = 0.5f;
    float mix = 1.0f;
};

class StereoInsert
{
public:
    void prepare (double sr) { fs = (float) sr; reset(); }
    void reset() { for (auto& s : st) s = {}; ringPhase = 0.0f; }

    inline void process (float& l, float& r, const InsertParams& p, float noteHz)
    {
        if (p.type == InsOff) return;
        float dl = l, dr = r;
        l = run (st[0], l, p, noteHz);
        r = run (st[1], r, p, noteHz);
        if (p.type == InsRingMod)
        {
            ringPhase += (0.25f + p.p2 * 7.75f) * noteHz / fs;
            ringPhase -= std::floor (ringPhase);
        }
        l = dl + (l - dl) * p.mix;
        r = dr + (r - dr) * p.mix;
    }

private:
    struct S { float lp = 0, hold = 0, cnt = 0, bp1 = 0, bp2 = 0; };
    S st[2];
    float fs = 44100.0f, ringPhase = 0.0f;

    inline float run (S& s, float x, const InsertParams& p, float noteHz)
    {
        float drive = 1.0f + p.drive * 14.0f;
        switch (p.type)
        {
            case InsTube:
            {
                float y = x * drive + 0.15f * p.p2 * drive * x * x;   // slight asymmetry
                return std::tanh (y) * (1.0f / std::sqrt (drive * 0.35f + 0.65f));
            }
            case InsOverdrive:
            {
                float y = x * drive;
                return clampf (y - (y * y * y) * 0.1481f, -0.667f, 0.667f) * 1.4f / (0.8f + p.drive * 0.8f);
            }
            case InsToneFuzz:
            {
                float fc = 0.02f + p.p2 * p.p2 * 0.5f;
                s.lp += fc * (x - s.lp);
                float y = (x - s.lp * (1.0f - p.p3)) * drive * 2.0f;
                return clampf (y, -0.6f, 0.6f) * (1.0f / (0.6f + 0.1f * drive));
            }
            case InsBitcrush:
            {
                float bits = 16.0f - p.drive * 13.0f;
                float q = std::exp2 (bits - 1.0f);
                float rate = 1.0f + p.p2 * 30.0f;
                s.cnt += 1.0f;
                if (s.cnt >= rate) { s.cnt -= rate; s.hold = std::round (x * q) / q; }
                return s.hold;
            }
            case InsExciter:
            {
                float fc = 0.05f + p.p2 * 0.6f;
                s.lp += fc * (x - s.lp);
                float hi = x - s.lp;
                return x + std::tanh (hi * (1.0f + drive * 0.5f)) * (0.3f + p.p3);
            }
            case InsRingMod:
            {
                (void) noteHz;
                return x * lerpf (1.0f, std::sin (kTwoPi * ringPhase), p.drive);
            }
            default: return x;
        }
    }
};
} // namespace stratum
