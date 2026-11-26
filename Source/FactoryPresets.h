#pragma once
// Stratum - built-in sounds. Each preset edits a default EngineParams snapshot.
#include "Params.h"
#include <functional>

namespace stratum
{
struct FactoryPreset
{
    const char* category;
    const char* name;
    std::function<void (EngineParams&)> build;
};

namespace fp
{
inline void osc (OscParams& o, int table, float morph, int semi, int unison, float detune, float vol, float spread = 0.6f)
{
    o.on = true; o.table = table; o.morph = morph; o.semi = semi; o.unison = unison; o.detune = detune; o.volume = vol; o.spread = spread;
}
inline void amp (LayerParams& P, float a, float d, float s, float r) { P.env[0].attack = a; P.env[0].decay = d; P.env[0].sustain = s; P.env[0].release = r; }
inline void flt (LayerParams& P, int type, float cutoff, float res, float envAmt) { P.filter.type = type; P.filter.cutoff = cutoff; P.filter.resonance = res; P.filter.envAmount = envAmt; }
inline void fenv (LayerParams& P, float a, float d, float s, float r) { P.env[1].attack = a; P.env[1].decay = d; P.env[1].sustain = s; P.env[1].release = r; }
inline void fx (LayerParams& P, int slot, int type, float mix, float a, float b, float c, float d)
{
    P.fx[slot].type = type; P.fx[slot].mix = mix; P.fx[slot].a = a; P.fx[slot].b = b; P.fx[slot].c = c; P.fx[slot].d = d;
}
inline void route (LayerParams& P, int slot, int src, int tgt, float amount) { P.mod[slot] = { true, src, tgt, amount }; }
inline void clearFx (LayerParams& P) { for (auto& f : P.fx) f.type = FxNone; }
inline void off (EngineParams& E, int fromLayer) { for (int l = fromLayer; l < kLayers; ++l) E.layer[l].enabled = false; }
} // namespace fp

inline const std::vector<FactoryPreset>& factoryPresets()
{
    // Just a blank starting point: save your own sounds with the Save button.
    static const std::vector<FactoryPreset> presets = {
        { "Init", "Init Saw", [] (EngineParams&) {} },
    };
    return presets;
}
} // namespace stratum
