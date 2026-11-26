#pragma once
// Furious - per-layer effect rack slots (no JUCE dependency).
// The effects are Airwindows plug-ins (MIT licence) ported with Tools/port_airwindows.py: see Airwindows/.
#include "Common.h"
#include "Airwindows/kRockstar2.h"
#include "Airwindows/Weave.h"
#include "Airwindows/DubSub2.h"
#include "Airwindows/kGuitarHall2.h"
#include "Airwindows/FatEQ.h"
#include "Airwindows/RetroBass.h"
#include "Airwindows/TakeCare.h"
#include "Airwindows/Disintegrate.h"
#include "Airwindows/CloudCoat.h"
#include "Airwindows/GlitchShifter.h"
#include <memory>
#include <vector>

namespace stratum
{
enum FxType
{
    FxNone = 0, FxHallReverb, FxWeave, FxDubSub, FxGuitarHall, FxFatEQ, FxRetroBass, FxTakeCare, FxDisintegrate,
    FxCloudCoat, FxGlitchShifter, FxNumTypes
};

inline const char* fxTypeName (int t)
{
    static const char* n[FxNumTypes] = { "----", "Hall Reverb", "Weave", "DubSub2", "Guitar Hall", "FatEQ", "RetroBass", "TakeCare",
                                          "Disintegrate", "CloudCoat", "GlitchShifter" };
    return n[std::min (std::max (t, 0), (int) FxNumTypes - 1)];
}

constexpr int kFxKnobs = 7;                  // control knobs a..g (the mix knob is separate)

// Names of the control knobs of an effect; an empty name hides the knob.
inline const char* fxParamLabel (int type, int idx)
{
    static const char* labels[FxNumTypes][kFxKnobs] = {
        { "", "", "", "", "", "", "" },
        { "Regen", "Position", "DownRez", "Distance", "", "", "" },                      // kRockstar2 (its Dry/Wet is Mix)
        { "Input", "Smooth", "Output", "", "", "", "" },                                 // Weave
        { "HeadBmp", "HeadFrq", "", "", "", "", "" },                                    // DubSub2
        { "Regen", "Derez", "Filter", "EarlyRF", "Position", "", "" },                   // kGuitarHall2
        { "High", "HMid", "LMid", "Bass", "Out", "", "" },                               // FatEQ
        { "Freq", "", "", "", "", "", "" },                                              // RetroBass
        { "Speed", "Rando", "Depth", "Regen", "Derez", "Buffer", "Output" },             // TakeCare
        { "Top dB", "BufSize", "Layers", "Filter", "", "", "" },                         // Disintegrate
        { "Select", "Sustain", "", "", "", "", "" },                                     // CloudCoat
        { "Note", "Trim", "Tighten", "Feedbck", "", "", "" } };                          // GlitchShifter
    return labels[std::min (std::max (type, 0), (int) FxNumTypes - 1)][std::min (std::max (idx, 0), kFxKnobs - 1)];
}

struct FxParams
{
    int type = FxNone;
    float mix = 0.35f;
    float a = 0.5f, b = 0.5f, c = 0.5f, d = 0.5f, e = 0.5f, f = 0.5f, g = 0.5f;
    float knob (int i) const { const float k[kFxKnobs] = { a, b, c, d, e, f, g }; return k[std::min (std::max (i, 0), kFxKnobs - 1)]; }
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
        resetAll();
    }

    void reset() { resetAll(); }

    void process (float* L, float* R, int n, const FxParams& p, const Transport&)
    {
        if (p.type != lastType) { lastType = p.type; clearTail (p.type); }
        switch (p.type)
        {
            case FxHallReverb:   run (rock, p, L, R, n, true); break;       // true: the effect has its own Dry/Wet
            case FxWeave:        run (weave, p, L, R, n, false); break;
            case FxDubSub:       run (dubsub, p, L, R, n, true); break;
            case FxGuitarHall:   run (guitarHall, p, L, R, n, true); break;
            case FxFatEQ:        run (fatEq, p, L, R, n, false); break;
            case FxRetroBass:    run (retroBass, p, L, R, n, false); break;
            case FxTakeCare:     run (takeCare, p, L, R, n, true); break;
            case FxDisintegrate: run (disintegrate, p, L, R, n, true); break;
            case FxCloudCoat:    run (cloudCoat, p, L, R, n, true); break;
            case FxGlitchShifter:run (glitch, p, L, R, n, true); break;
            default: break;
        }
    }

private:
    static constexpr int kChunk = 256;

    // Runs one Airwindows effect. Its parameters are the knobs a.. in order; when it has a Dry/Wet parameter (always the
    // last one) the Mix knob drives that, otherwise the dry and wet signals are blended here.
    template <class T>
    void run (std::unique_ptr<T>& fx, const FxParams& p, float* L, float* R, int n, bool ownDryWet)
    {
        if (! fx) { fx = std::make_unique<T>(); fx->setSampleRate (fs); }       // first use of this effect: allocate
        constexpr int knobs = T::kNumParams;
        const int count = ownDryWet ? knobs - 1 : knobs;
        for (int i = 0; i < count; ++i) fx->setParam (i, p.knob (i));
        if (ownDryWet) { fx->setParam (knobs - 1, p.mix); fx->process (L, R, n); return; }
        float dl[kChunk], dr[kChunk];
        for (int off = 0; off < n; off += kChunk)
        {
            const int m = std::min (kChunk, n - off);
            std::copy (L + off, L + off + m, dl); std::copy (R + off, R + off + m, dr);
            fx->process (L + off, R + off, m);
            for (int i = 0; i < m; ++i)
            {
                L[off + i] = dl[i] + (L[off + i] - dl[i]) * p.mix;
                R[off + i] = dr[i] + (R[off + i] - dr[i]) * p.mix;
            }
        }
    }

    template <class T> void wipe (std::unique_ptr<T>& fx) { if (fx) { fx->reset(); fx->setSampleRate (fs); } }
    void resetAll()
    {
        wipe (rock); wipe (weave); wipe (dubsub); wipe (guitarHall); wipe (fatEq); wipe (retroBass);
        wipe (takeCare); wipe (disintegrate); wipe (cloudCoat); wipe (glitch);
        lastType = FxNone;
    }
    // an effect that was switched off or swapped must not leave its old tail behind
    void clearTail (int t)
    {
        if (t != FxHallReverb) wipe (rock);
        if (t != FxWeave) wipe (weave);
        if (t != FxDubSub) wipe (dubsub);
        if (t != FxGuitarHall) wipe (guitarHall);
        if (t != FxFatEQ) wipe (fatEq);
        if (t != FxRetroBass) wipe (retroBass);
        if (t != FxTakeCare) wipe (takeCare);
        if (t != FxDisintegrate) wipe (disintegrate);
        if (t != FxCloudCoat) wipe (cloudCoat);
        if (t != FxGlitchShifter) wipe (glitch);
    }

    double fs = 44100.0;
    int lastType = FxNone;
    std::unique_ptr<aw::kRockstar2> rock;
    std::unique_ptr<aw::Weave> weave;
    std::unique_ptr<aw::DubSub2> dubsub;
    std::unique_ptr<aw::kGuitarHall2> guitarHall;
    std::unique_ptr<aw::FatEQ> fatEq;
    std::unique_ptr<aw::RetroBass> retroBass;
    std::unique_ptr<aw::TakeCare> takeCare;
    std::unique_ptr<aw::Disintegrate> disintegrate;
    std::unique_ptr<aw::CloudCoat> cloudCoat;
    std::unique_ptr<aw::GlitchShifter> glitch;
};
} // namespace stratum
