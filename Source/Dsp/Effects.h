#pragma once
// Stratum - per-layer effect rack slots (no JUCE dependency).
#include "Common.h"
#include <vector>

namespace stratum
{
enum FxType
{
    FxNone = 0, FxReverb, FxDelay, FxChorus, FxPhaser, FxDistortion, FxBitcrush,
    FxTrancegate, FxCompressor, FxSidechain, FxBalancer, FxShimmer, FxNumTypes
};

inline const char* fxTypeName (int t)
{
    static const char* n[FxNumTypes] = { "----", "Reverb", "Delay", "Chorus", "Phaser", "Distortion",
                                         "Bitcrush", "Trancegate", "Compressor", "Sidechain", "Balancer", "Shimmer Reverb" };
    return n[std::min (std::max (t, 0), (int) FxNumTypes - 1)];
}

inline const char* fxParamLabel (int type, int idx)
{
    static const char* labels[FxNumTypes][4] = {
        { "", "", "", "" },
        { "Size", "Damp", "Width", "Predelay" },
        { "Time Scale", "Feedback", "Tone", "Ping-Pong" },
        { "Rate", "Depth", "Stereo", "Feedback" },
        { "Rate", "Depth", "Feedback", "Stereo" },
        { "Drive", "Tone", "Post Gain", "Bias" },
        { "Bits", "Rate", "Jitter", "" },
        { "Smooth", "Shuffle", "Depth", "" },
        { "Threshold", "Ratio", "Release", "Makeup" },
        { "Depth", "Length", "Shape", "Offset" },
        { "Volume", "Pan", "Width", "" },
        { "Size", "Damp", "Shimmer", "Pitch" } };
    return labels[std::min (std::max (type, 0), (int) FxNumTypes - 1)][idx & 3];
}

struct FxParams
{
    int type = FxNone;
    float mix = 0.35f;
    float a = 0.5f, b = 0.5f, c = 0.5f, d = 0.5f;
    int div = 6;                          // tempo division (delay time / gate speed / sidechain rate); the delay's left time
    int divR = 0;                         // the delay's right time: 0 = same as left, otherwise division index + 1
    int gateLength = 16;
    float gate[2][kMaxSteps] = {};        // trancegate lanes, 0 or 1
};

// Moving the effect in slot `from` to slot `to` (the ones in between shift by one). Fills src[i] with the old slot
// whose contents end up in slot i.
inline void fxMoveOrder (int from, int to, int* src)
{
    from = std::min (std::max (from, 0), kFxSlots - 1);
    to = std::min (std::max (to, 0), kFxSlots - 1);
    int tmp[kFxSlots], n = 0;
    for (int i = 0; i < kFxSlots; ++i) if (i != from) tmp[n++] = i;
    for (int i = 0, j = 0; i < kFxSlots; ++i) src[i] = i == to ? from : tmp[j++];
}
inline int fxNewPosition (int oldSlot, int from, int to)
{
    int src[kFxSlots];
    fxMoveOrder (from, to, src);
    for (int i = 0; i < kFxSlots; ++i) if (src[i] == oldSlot) return i;
    return oldSlot;
}

class FxSlot
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        int dl = (int) (fs * 1.0f) + 8;
        for (auto& d : delay) d.assign ((size_t) dl, 0.0f);
        delayLen = dl;
        for (auto& d : pre) d.assign ((size_t) (fs * 0.1f) + 8, 0.0f);
        for (auto& d : cho) d.assign ((size_t) (fs * 0.06f) + 8, 0.0f);
        shWin = fs * 0.085f;
        for (auto& d : shBuf) d.assign ((size_t) shWin + 16, 0.0f);
        float sc = fs / 44100.0f;
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4] = { 556, 441, 341, 225 };
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < 8; ++i) { comb[c][i].buf.assign ((size_t) ((combT[i] + (c ? 23 : 0)) * sc) + 4, 0.0f); comb[c][i].idx = 0; comb[c][i].lp = 0; }
            for (int i = 0; i < 4; ++i) { ap[c][i].buf.assign ((size_t) ((apT[i] + (c ? 23 : 0)) * sc) + 4, 0.0f); ap[c][i].idx = 0; }
        }
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : pre) std::fill (d.begin(), d.end(), 0.0f);
        for (auto& d : cho) std::fill (d.begin(), d.end(), 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            for (auto& x : comb[c]) { std::fill (x.buf.begin(), x.buf.end(), 0.0f); x.lp = 0; }
            for (auto& x : ap[c]) std::fill (x.buf.begin(), x.buf.end(), 0.0f);
            for (auto& s : apState[c]) s = 0.0f;
            fbState[c] = 0.0f; toneLp[c] = 0.0f; holdS[c] = 0.0f;
        }
        for (auto& d : shBuf) std::fill (d.begin(), d.end(), 0.0f);
        shPos = 0; shPh = 0.0f; shFb[0] = shFb[1] = 0.0f; shLp[0] = shLp[1] = 0.0f;
        dPos = preIdx = choIdx = 0;
        lfoPh = 0.0; gateGain[0] = gateGain[1] = 1.0f; env = 0.0f;
    }

    void process (float* L, float* R, int n, const FxParams& p, const Transport& tp)
    {
        if (p.type == FxNone) return;
        double beat = tp.ppq;
        double beatInc = tp.bpm / 60.0 / (double) fs;
        for (int i = 0; i < n; ++i, beat += beatInc)
        {
            float dl = L[i], dr = R[i], wl = dl, wr = dr;
            switch (p.type)
            {
                case FxReverb: reverb (dl, dr, wl, wr, p); break;
                case FxDelay: delayFx (dl, dr, wl, wr, p, tp.bpm); break;
                case FxChorus: chorus (dl, dr, wl, wr, p); break;
                case FxPhaser: phaser (dl, dr, wl, wr, p); break;
                case FxDistortion: distortion (dl, dr, wl, wr, p); break;
                case FxBitcrush: bitcrush (dl, dr, wl, wr, p); break;
                case FxTrancegate: trancegate (dl, dr, wl, wr, p, beat); break;
                case FxCompressor: compressor (dl, dr, wl, wr, p); break;
                case FxSidechain: sidechain (wl, wr, p, beat); break;
                case FxBalancer: balancer (dl, dr, wl, wr, p); break;
                case FxShimmer: shimmer (dl, dr, wl, wr, p); break;
                default: break;
            }
            L[i] = dl + (wl - dl) * p.mix;
            R[i] = dr + (wr - dr) * p.mix;
        }
    }

private:
    float fs = 44100.0f;
    std::vector<float> delay[2], pre[2], cho[2];
    int delayLen = 0, dPos = 0, preIdx = 0, choIdx = 0;
    struct Comb { std::vector<float> buf; int idx = 0; float lp = 0; };
    struct Ap { std::vector<float> buf; int idx = 0; };
    Comb comb[2][8];
    Ap ap[2][4];
    float apState[2][6] = {}, fbState[2] = {}, toneLp[2] = {}, holdS[2] = {};
    std::vector<float> shBuf[2];                // pitch shifter inside the shimmer reverb's feedback loop
    float shWin = 3750.0f, shPh = 0.0f, shFb[2] = {}, shLp[2] = {};
    int shPos = 0;
    double lfoPh = 0.0;
    float gateGain[2] = { 1, 1 };
    float env = 0.0f, holdCnt = 0.0f;
    Rng rng;

    static float readFrac (const std::vector<float>& b, int pos, float delaySamples)
    {
        int len = (int) b.size();
        float rp = (float) pos - delaySamples;
        while (rp < 0) rp += (float) len;
        int i0 = (int) rp;
        float fr = rp - (float) i0;
        i0 %= len;
        int i1 = (i0 + 1) % len;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }

    void reverb (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float predelay = p.d * 0.08f * fs;
        pre[0][(size_t) preIdx] = dl; pre[1][(size_t) preIdx] = dr;
        float inL = readFrac (pre[0], preIdx, predelay), inR = readFrac (pre[1], preIdx, predelay);
        preIdx = (preIdx + 1) % (int) pre[0].size();
        float in = (inL + inR) * 0.015f;
        float fb = 0.7f + 0.28f * p.a, damp = p.b * 0.4f;
        float out[2] = { 0, 0 };
        for (int c = 0; c < 2; ++c)
        {
            for (auto& cb : comb[c])
            {
                float y = cb.buf[(size_t) cb.idx];
                cb.lp = y * (1.0f - damp) + cb.lp * damp;
                cb.buf[(size_t) cb.idx] = in + cb.lp * fb;
                if (++cb.idx >= (int) cb.buf.size()) cb.idx = 0;
                out[c] += y;
            }
            for (auto& a : ap[c])
            {
                float y = a.buf[(size_t) a.idx];
                a.buf[(size_t) a.idx] = out[c] + y * 0.5f;
                out[c] = y - out[c];
                if (++a.idx >= (int) a.buf.size()) a.idx = 0;
            }
        }
        float w = p.c;
        wl = out[0] * (0.5f + 0.5f * w) + out[1] * (0.5f - 0.5f * w);
        wr = out[1] * (0.5f + 0.5f * w) + out[0] * (0.5f - 0.5f * w);
        wl *= 3.0f; wr *= 3.0f;
    }

    // Reverb whose tail is fed back through an up-shifting pitch shifter, so every repeat climbs.
    void shimmer (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float in = (dl + dr) * 0.015f;
        const float fb = 0.72f + 0.26f * p.a, damp = p.b * 0.4f;
        const float amount = p.c * 0.7f;
        const float semis = p.d * 24.0f;                          // 0 .. 24 semitones up, 12 in the middle
        const float ratio = std::exp2 (semis / 12.0f);
        float out[2] = { 0, 0 };
        const float inL = in + shFb[0] * 0.015f, inR = in + shFb[1] * 0.015f;
        for (int c = 0; c < 2; ++c)
        {
            const float x = c == 0 ? inL : inR;
            for (auto& cb : comb[c])
            {
                float y = cb.buf[(size_t) cb.idx];
                cb.lp = y * (1.0f - damp) + cb.lp * damp;
                cb.buf[(size_t) cb.idx] = x + cb.lp * fb;
                if (++cb.idx >= (int) cb.buf.size()) cb.idx = 0;
                out[c] += y;
            }
            for (auto& a : ap[c])
            {
                float y = a.buf[(size_t) a.idx];
                a.buf[(size_t) a.idx] = out[c] + y * 0.5f;
                out[c] = y - out[c];
                if (++a.idx >= (int) a.buf.size()) a.idx = 0;
            }
        }
        // pitch shift the tail with two crossfaded read heads
        float sh[2];
        const float w1 = std::sin (kPi * shPh), w2 = std::cos (kPi * shPh);
        const float d1 = shPh * shWin, d2 = oscFracLocal (shPh + 0.5f) * shWin;
        for (int c = 0; c < 2; ++c)
        {
            shBuf[c][(size_t) shPos] = out[c] * 3.0f;
            sh[c] = readFrac (shBuf[c], shPos, d1) * w1 * w1 + readFrac (shBuf[c], shPos, d2) * w2 * w2;
        }
        shPos = (shPos + 1) % (int) shBuf[0].size();
        shPh = oscFracLocal (shPh - (ratio - 1.0f) / shWin);
        for (int c = 0; c < 2; ++c)
        {
            shLp[c] += 0.45f * (sh[c] - shLp[c]);                // soften the shifted copy
            shFb[c] = std::tanh (shLp[c]) * amount;
        }
        const float w = 0.8f;
        wl = (out[0] * (0.5f + 0.5f * w) + out[1] * (0.5f - 0.5f * w)) * 3.0f;
        wr = (out[1] * (0.5f + 0.5f * w) + out[0] * (0.5f - 0.5f * w)) * 3.0f;
    }
    static float oscFracLocal (float x) { return x - std::floor (x); }

    void delayFx (float dl, float dr, float& wl, float& wr, const FxParams& p, double bpm)
    {
        // each channel has its own tempo division; Time Scale stretches both (x0.5 .. x2, exactly x1 in the middle)
        const float beatSecs = (float) (60.0 / std::max (bpm, 20.0)), scale = std::exp2 ((p.a - 0.5f) * 2.0f);
        const int dL = std::min (std::max (p.div, 0), kNumDivs - 1);
        const int dR = p.divR <= 0 ? dL : std::min (p.divR - 1, kNumDivs - 1);
        float d = clampf (beatSecs / kDivCyclesPerBeat[dL] * scale, 0.002f, 0.98f) * fs;
        float dRight = clampf (beatSecs / kDivCyclesPerBeat[dR] * scale, 0.002f, 0.98f) * fs;
        float yl = readFrac (delay[0], dPos, d);
        float yr = readFrac (delay[1], dPos, dRight);
        float tone = 0.05f + p.c * 0.9f;
        toneLp[0] += tone * (yl - toneLp[0]);
        toneLp[1] += tone * (yr - toneLp[1]);
        float fb = clampf (p.b, 0.0f, 0.95f);
        float pp = p.d;
        float fl = toneLp[0], fr = toneLp[1];
        delay[0][(size_t) dPos] = dl + (fl * (1.0f - pp) + fr * pp) * fb;
        delay[1][(size_t) dPos] = dr + (fr * (1.0f - pp) + fl * pp) * fb;
        dPos = (dPos + 1) % delayLen;
        wl = dl + yl; wr = dr + yr;
    }

    void chorus (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float rate = 0.1f + p.a * 4.0f;
        lfoPh += rate / fs; if (lfoPh >= 1.0) lfoPh -= 1.0;
        float depth = p.b * 0.006f * fs;
        float base = 0.012f * fs;
        float mL = std::sin ((float) lfoPh * kTwoPi);
        float mR = std::sin (((float) lfoPh + 0.25f * p.c) * kTwoPi);
        cho[0][(size_t) choIdx] = dl + fbState[0] * p.d * 0.6f;
        cho[1][(size_t) choIdx] = dr + fbState[1] * p.d * 0.6f;
        float yl = readFrac (cho[0], choIdx, base + depth * mL);
        float yr = readFrac (cho[1], choIdx, base + depth * mR);
        fbState[0] = yl; fbState[1] = yr;
        choIdx = (choIdx + 1) % (int) cho[0].size();
        wl = (dl + yl) * 0.7f; wr = (dr + yr) * 0.7f;
    }

    void phaser (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float rate = 0.05f + p.a * 3.0f;
        lfoPh += rate / fs; if (lfoPh >= 1.0) lfoPh -= 1.0;
        float mod[2] = { 0.5f + 0.5f * std::sin ((float) lfoPh * kTwoPi),
                         0.5f + 0.5f * std::sin (((float) lfoPh + 0.25f * p.d) * kTwoPi) };
        float in[2] = { dl, dr }, out[2];
        for (int c = 0; c < 2; ++c)
        {
            float fc = 200.0f * std::pow (20.0f, clampf (0.15f + p.b * mod[c] * 0.85f, 0.0f, 1.0f));
            float t = std::tan (kPi * std::min (fc, 0.45f * fs) / fs);
            float a = (t - 1.0f) / (t + 1.0f);
            float x = in[c] + fbState[c] * p.c * 0.9f;
            for (int s = 0; s < 6; ++s)
            {
                float y = a * x + apState[c][s];
                apState[c][s] = x - a * y;
                x = y;
            }
            fbState[c] = std::tanh (x);
            out[c] = (in[c] + x) * 0.5f;
        }
        wl = out[0]; wr = out[1];
    }

    void distortion (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float drive = 1.0f + p.a * 20.0f, bias = (p.d - 0.5f) * 0.6f;
        float post = dbToGain ((p.c - 0.5f) * 24.0f) / std::sqrt (drive);
        float tone = 0.03f + p.b * p.b * 0.95f;
        float in[2] = { dl, dr }, out[2];
        for (int c = 0; c < 2; ++c)
        {
            float y = std::tanh (in[c] * drive + bias) - std::tanh (bias);
            toneLp[c] += tone * (y - toneLp[c]);
            out[c] = toneLp[c] * post * 1.4f;
        }
        wl = out[0]; wr = out[1];
    }

    void bitcrush (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float q = std::exp2 (16.0f - p.a * 14.0f);
        float rate = 1.0f + p.b * 40.0f + rng.next01() * p.c * 20.0f;
        holdCnt += 1.0f;
        if (holdCnt >= rate)
        {
            holdCnt -= rate;
            holdS[0] = std::round (dl * q) / q;
            holdS[1] = std::round (dr * q) / q;
        }
        wl = holdS[0]; wr = holdS[1];
    }

    void trancegate (float dl, float dr, float& wl, float& wr, const FxParams& p, double beat)
    {
        float cyc = kDivCyclesPerBeat[std::min (std::max (p.div, 0), kNumDivs - 1)];
        int len = std::max (1, std::min (p.gateLength, kMaxSteps));
        double sp = beat * (double) cyc;
        int idx = (int) std::floor (sp);
        float frac = (float) (sp - (double) idx);
        idx = ((idx % len) + len) % len;
        // shuffle: delay even steps
        if ((idx & 1) && frac < p.b * 0.5f) idx = (idx - 1 + len) % len;
        float coef = 1.0f - std::exp (-1.0f / (fs * (0.0003f + p.a * 0.02f)));
        for (int c = 0; c < 2; ++c)
        {
            float target = p.gate[c][idx] > 0.5f ? 1.0f : (1.0f - p.c);
            gateGain[c] += (target - gateGain[c]) * coef;
        }
        wl = dl * gateGain[0]; wr = dr * gateGain[1];
    }

    void compressor (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float thr = -40.0f + p.a * 40.0f;
        float ratio = 1.0f + p.b * 19.0f;
        float rel = std::exp (-1.0f / (fs * (0.02f + p.c * 0.5f)));
        float att = std::exp (-1.0f / (fs * 0.003f));
        float lvl = std::max (std::fabs (dl), std::fabs (dr));
        env = lvl > env ? att * env + (1.0f - att) * lvl : rel * env + (1.0f - rel) * lvl;
        float db = 20.0f * std::log10 (std::max (env, 1.0e-6f));
        float over = std::max (0.0f, db - thr);
        float gr = -over * (1.0f - 1.0f / ratio);
        float g = dbToGain (gr + p.d * 18.0f);
        wl = dl * g; wr = dr * g;
    }

    void sidechain (float& wl, float& wr, const FxParams& p, double beat)
    {
        float cyc = kDivCyclesPerBeat[std::min (std::max (p.div, 0), kNumDivs - 1)];
        float ph = (float) std::fmod (beat * (double) cyc, 1.0);
        ph = ph - p.d * 0.5f; if (ph < 0) ph += 1.0f;
        float len = 0.1f + p.b * 0.9f;
        float x = clampf (ph / len, 0.0f, 1.0f);
        float g = 1.0f - p.a + p.a * std::pow (x, 0.3f + p.c * 3.0f);
        wl *= g; wr *= g;
    }

    void balancer (float dl, float dr, float& wl, float& wr, const FxParams& p)
    {
        float vol = p.a * 2.0f;
        float m = (dl + dr) * 0.5f, s = (dl - dr) * 0.5f * (p.c * 2.0f);
        float l = m + s, r = m - s;
        float pan = (p.b - 0.5f) * 2.0f;
        wl = l * vol * (pan > 0 ? 1.0f - pan : 1.0f);
        wr = r * vol * (pan < 0 ? 1.0f + pan : 1.0f);
    }
};
} // namespace stratum
