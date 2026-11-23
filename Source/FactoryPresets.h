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
inline void fx (LayerParams& P, int slot, int type, float mix, float a, float b, float c, float d, int div = 6)
{
    P.fx[slot].type = type; P.fx[slot].mix = mix; P.fx[slot].a = a; P.fx[slot].b = b; P.fx[slot].c = c; P.fx[slot].d = d; P.fx[slot].div = div;
}
inline void route (LayerParams& P, int slot, int src, int tgt, float amount) { P.mod[slot] = { true, src, tgt, amount }; }
inline void clearFx (LayerParams& P) { for (auto& f : P.fx) f.type = FxNone; }
inline void off (EngineParams& E, int fromLayer) { for (int l = fromLayer; l < kLayers; ++l) E.layer[l].enabled = false; }
} // namespace fp

inline const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace fp;
    static const std::vector<FactoryPreset> presets = {
        { "Init", "Init Saw", [] (EngineParams&) {} },

        { "Bass", "Deep Sub", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 0, 0.15f, -12, 1, 0.0f, 0.9f);
              osc (P.osc[1], 1, 0.3f, -24, 1, 0.0f, 0.55f);
              P.osc[1].on = true;
              amp (P, 0.002f, 0.25f, 0.9f, 0.12f);
              flt (P, FltLadder, 0.36f, 0.25f, 0.3f); fenv (P, 0.001f, 0.2f, 0.25f, 0.2f);
              P.insert.type = InsTube; P.insert.drive = 0.25f;
              P.voicing.mono = true; P.voicing.glideTime = 0.06f;
              clearFx (P); fx (P, 0, FxCompressor, 1.0f, 0.55f, 0.5f, 0.3f, 0.3f);
              off (E, 1);
          } },

        { "Bass", "Wobble Dub", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 0, 0.9f, -12, 3, 0.18f, 0.8f);
              osc (P.osc[1], 3, 0.2f, -24, 1, 0.0f, 0.5f);
              amp (P, 0.002f, 0.2f, 0.85f, 0.15f);
              flt (P, FltLP24, 0.3f, 0.55f, 0.0f);
              P.lfo[0].sync = true; P.lfo[0].syncDiv = 5; P.lfo[0].waveA = 1; P.lfo[0].bipolar = true;
              route (P, 0, SrcLfo1, TgCutoff, 0.35f);
              route (P, 1, SrcLfo1, TgOscMorph1, 0.3f);
              clearFx (P); fx (P, 0, FxDistortion, 0.6f, 0.45f, 0.6f, 0.5f, 0.5f); fx (P, 1, FxReverb, 0.1f, 0.3f, 0.5f, 0.7f, 0.0f);
              P.voicing.mono = true; P.voicing.glideTime = 0.04f;
              off (E, 1);
          } },

        { "Arp", "Disco Arp", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 0, 1.0f, 0, 2, 0.15f, 0.7f);
              osc (P.osc[1], 1, 0.6f, 12, 1, 0.0f, 0.3f);
              amp (P, 0.001f, 0.18f, 0.0f, 0.12f);
              flt (P, FltLP24, 0.35f, 0.3f, 0.55f); fenv (P, 0.001f, 0.16f, 0.0f, 0.1f);
              P.arp.on = true; P.arp.order = ArpUpDown; P.arp.octaves = 2; P.arp.div = 6; P.arp.gateLength = 0.55f;
              clearFx (P); fx (P, 0, FxDelay, 0.3f, 0.75f, 0.4f, 0.6f, 0.6f); fx (P, 1, FxChorus, 0.35f, 0.3f, 0.4f, 0.6f, 0.0f); fx (P, 2, FxReverb, 0.2f, 0.55f, 0.4f, 0.8f, 0.1f);
              LayerParams& Q = E.layer[1];
              Q.enabled = true; Q.gainDb = -14.0f;
              osc (Q.osc[0], 1, 0.0f, -12, 1, 0.0f, 0.8f);
              amp (Q, 0.002f, 0.25f, 0.0f, 0.1f); flt (Q, FltLP12, 0.3f, 0.1f, 0.2f);
              Q.arp.on = true; Q.arp.order = ArpUp; Q.arp.div = 5; Q.arp.gateLength = 0.4f;
              clearFx (Q);
              off (E, 2);
          } },

        { "Pad", "Sweep Pad", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 0, 1.0f, 0, 6, 0.35f, 0.6f, 0.9f);
              osc (P.osc[1], 3, 0.5f, 0, 4, 0.25f, 0.45f, 0.8f);
              P.osc[0].random = 1.0f; P.osc[1].random = 1.0f;
              amp (P, 1.2f, 1.0f, 0.8f, 2.4f);
              flt (P, FltLP12, 0.42f, 0.2f, 0.35f); fenv (P, 2.0f, 2.0f, 0.4f, 2.0f);
              P.lfo[0].rateHz = 0.18f; P.lfo[0].waveA = 0;
              route (P, 0, SrcLfo1, TgOscMorph2, 0.5f); route (P, 1, SrcLfo1, TgCutoff, 0.12f);
              clearFx (P); fx (P, 0, FxChorus, 0.45f, 0.2f, 0.5f, 0.8f, 0.2f); fx (P, 1, FxPhaser, 0.3f, 0.15f, 0.6f, 0.4f, 0.5f); fx (P, 2, FxReverb, 0.4f, 0.85f, 0.45f, 1.0f, 0.2f);
              off (E, 1);
          } },

        { "Pluck", "Glass Bells", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 5, 0.4f, 0, 2, 0.1f, 0.8f);
              osc (P.osc[1], 6, 0.5f, 12, 1, 0.0f, 0.35f);
              amp (P, 0.001f, 1.4f, 0.0f, 1.2f);
              flt (P, FltLP12, 0.75f, 0.1f, 0.0f);
              P.env[1].decay = 1.0f;
              route (P, 0, SrcEnv3, TgOscMorph1, 0.5f); P.env[2].attack = 0.001f; P.env[2].decay = 0.9f; P.env[2].sustain = 0.0f;
              clearFx (P); fx (P, 0, FxDelay, 0.3f, 0.9f, 0.5f, 0.55f, 0.7f, 5); fx (P, 1, FxReverb, 0.35f, 0.8f, 0.4f, 1.0f, 0.1f);
              off (E, 1);
          } },

        { "Lead", "Gate Lead", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 0, 1.0f, 0, 5, 0.3f, 0.7f, 0.9f);
              osc (P.osc[1], 1, 0.7f, 0, 3, 0.2f, 0.4f, 0.8f);
              amp (P, 0.004f, 0.3f, 0.9f, 0.35f);
              flt (P, FltLP24, 0.58f, 0.2f, 0.25f);
              clearFx (P);
              fx (P, 0, FxTrancegate, 1.0f, 0.25f, 0.1f, 0.0f, 0.5f, 6); P.fx[0].gateLength = 16;
              const int pat[16] = { 1, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 0 };
              for (int s = 0; s < 32; ++s) { P.fx[0].gate[0][s] = (float) pat[s % 16]; P.fx[0].gate[1][s] = (float) pat[(s + 3) % 16]; }
              fx (P, 1, FxDelay, 0.3f, 0.75f, 0.45f, 0.6f, 0.6f); fx (P, 2, FxReverb, 0.25f, 0.65f, 0.4f, 0.9f, 0.1f);
              off (E, 1);
          } },

        { "Pad", "Vowel Choir", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 4, 0.2f, 0, 5, 0.2f, 0.8f, 0.9f);
              osc (P.osc[1], 4, 0.8f, 12, 3, 0.15f, 0.3f, 0.8f);
              P.osc[0].random = 1.0f;
              amp (P, 0.8f, 1.0f, 0.85f, 1.8f);
              flt (P, FltFormant, 0.5f, 0.3f, 0.0f); P.filter.width = 0.25f;
              P.lfo[0].rateHz = 0.12f; P.lfo[0].waveA = 1;
              route (P, 0, SrcLfo1, TgOscMorph1, 0.6f); route (P, 1, SrcLfo1, TgOscMorph2, -0.6f);
              clearFx (P); fx (P, 0, FxChorus, 0.4f, 0.25f, 0.5f, 0.8f, 0.0f); fx (P, 1, FxReverb, 0.45f, 0.9f, 0.5f, 1.0f, 0.2f);
              off (E, 1);
          } },

        { "FX", "Grit Drone", [] (EngineParams& E)
          {
              LayerParams& P = E.layer[0];
              osc (P.osc[0], 7, 0.4f, -12, 3, 0.4f, 0.7f);
              P.osc[0].fx = OfxHardSync; P.osc[0].fxAmt = 0.35f;
              osc (P.osc[1], 2, 0.5f, -24, 2, 0.2f, 0.5f);
              P.osc[1].fx = OfxPhaseMod; P.osc[1].fxAmt = 0.4f;
              amp (P, 1.5f, 1.0f, 1.0f, 3.0f);
              flt (P, FltComb, 0.4f, 0.55f, 0.0f); P.filter.width = 0.4f;
              P.insert.type = InsRingMod; P.insert.drive = 0.3f; P.insert.p2 = 0.2f; P.insert.mix = 0.5f;
              P.lfo[0].rateHz = 0.07f; P.lfo[0].waveA = 6;
              route (P, 0, SrcLfo1, TgCutoff, 0.25f); route (P, 1, SrcLfo1, TgOscMorph1, 0.5f);
              clearFx (P); fx (P, 0, FxPhaser, 0.5f, 0.1f, 0.8f, 0.6f, 0.6f); fx (P, 1, FxBitcrush, 0.25f, 0.35f, 0.2f, 0.1f, 0.0f); fx (P, 2, FxReverb, 0.5f, 0.95f, 0.5f, 1.0f, 0.3f);
              off (E, 1);
          } },
    };
    return presets;
}
} // namespace stratum
