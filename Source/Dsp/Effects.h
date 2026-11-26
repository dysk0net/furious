#pragma once
// Furious - per-layer effect rack slots (no JUCE dependency).
// The effects are Airwindows plug-ins (MIT licence) ported with Tools/port_airwindows.py: see Airwindows/.
#include "Common.h"
#include "Airwindows/kRockstar2.h"
#include "Airwindows/Weave.h"
#include <memory>
#include <vector>

namespace stratum
{
enum FxType
{
    FxNone = 0, FxHallReverb, FxWeave, FxNumTypes
};

inline const char* fxTypeName (int t)
{
    static const char* n[FxNumTypes] = { "----", "Hall Reverb", "Weave" };
    return n[std::min (std::max (t, 0), (int) FxNumTypes - 1)];
}

// Names of the four knobs a..d of an effect (the mix knob is separate); an empty name hides the knob.
inline const char* fxParamLabel (int type, int idx)
{
    static const char* labels[FxNumTypes][4] = {
        { "", "", "", "" },
        { "Regen", "Position", "DownRez", "Distance" },     // Airwindows kRockstar2; its Dry/Wet is the Mix knob
        { "Input", "Smooth", "Output", "" } };               // Airwindows Weave
    return labels[std::min (std::max (type, 0), (int) FxNumTypes - 1)][idx & 3];
}

struct FxParams
{
    int type = FxNone;
    float mix = 0.35f;
    float a = 0.5f, b = 0.5f, c = 0.5f, d = 0.5f;
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
        fs = sr;
        if (rock) rock->setSampleRate (fs);
        if (weave) weave->setSampleRate (fs);
        reset();
    }

    void reset()
    {
        if (rock) { rock->reset(); rock->setSampleRate (fs); }
        if (weave) { weave->reset(); weave->setSampleRate (fs); }
        lastType = FxNone;
    }

    void process (float* L, float* R, int n, const FxParams& p, const Transport&)
    {
        if (p.type != lastType) { lastType = p.type; clearTail (p.type); }
        switch (p.type)
        {
            case FxHallReverb:
            {
                if (! rock) { rock = std::make_unique<aw::kRockstar2>(); rock->setSampleRate (fs); }   // first use of this effect: allocate
                rock->setParam (0, p.a); rock->setParam (1, p.b); rock->setParam (2, p.c); rock->setParam (3, p.d);
                rock->setParam (4, p.mix);                          // the reverb has its own dry/wet
                rock->process (L, R, n);
                break;
            }
            case FxWeave:
            {
                if (! weave) { weave = std::make_unique<aw::Weave>(); weave->setSampleRate (fs); }
                weave->setParam (0, p.a); weave->setParam (1, p.b); weave->setParam (2, p.c);
                float dl[kChunk], dr[kChunk];
                for (int off = 0; off < n; off += kChunk)
                {
                    const int m = std::min (kChunk, n - off);
                    std::copy (L + off, L + off + m, dl); std::copy (R + off, R + off + m, dr);
                    weave->process (L + off, R + off, m);
                    for (int i = 0; i < m; ++i)                     // dry/wet
                    {
                        L[off + i] = dl[i] + (L[off + i] - dl[i]) * p.mix;
                        R[off + i] = dr[i] + (R[off + i] - dr[i]) * p.mix;
                    }
                }
                break;
            }
            default: break;
        }
    }

private:
    static constexpr int kChunk = 256;

    // an effect that was switched off or swapped must not leave its old tail behind
    void clearTail (int newType)
    {
        if (newType != FxHallReverb && rock) { rock->reset(); rock->setSampleRate (fs); }
        if (newType != FxWeave && weave) { weave->reset(); weave->setSampleRate (fs); }
    }

    double fs = 44100.0;
    int lastType = FxNone;
    std::unique_ptr<aw::kRockstar2> rock;
    std::unique_ptr<aw::Weave> weave;
};
} // namespace stratum
