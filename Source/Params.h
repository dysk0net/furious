#pragma once
// Stratum - host parameter definitions.
// A single description of every parameter (ParamBinder) is used three ways:
//   1. build the AudioProcessorValueTreeState layout,
//   2. link each parameter's atomic value to a field of the DSP engine's parameter struct,
//   3. push a whole EngineParams snapshot (factory preset) back into the host parameters.
#include <JuceHeader.h>
#include "Dsp/Layer.h"
#include <vector>

namespace stratum
{
using APVTS = juce::AudioProcessorValueTreeState;

// Non-parameter state (sequencer steps etc.) that is saved with the preset.
inline juce::String layerId (int layer, const juce::String& suffix) { return "L" + juce::String (layer + 1) + "_" + suffix; }

class ParamBinder
{
public:
    struct Entry
    {
        std::atomic<float>* src = nullptr;
        juce::RangedAudioParameter* param = nullptr;
        size_t offset = 0;
        int kind = 0;   // 0 float, 1 int, 2 bool
    };

    ParamBinder (APVTS::ParameterLayout& l, EngineParams& base) : layout (&l), baseObj (&base) {}
    ParamBinder (APVTS& s, EngineParams& base) : apvts (&s), baseObj (&base) {}

    void setPrefix (const juce::String& idP, const juce::String& nameP) { idPrefix = idP; namePrefix = nameP; }

    // The current value of dst is used as the parameter's default.
    void f (const juce::String& id, const juce::String& name, float& dst, float lo, float hi, float mid = -1.0f)
    {
        if (layout)
        {
            juce::NormalisableRange<float> r (lo, hi);
            if (mid > lo && mid < hi) r.setSkewForCentre (mid);
            layout->add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { idPrefix + id, 1 }, namePrefix + name, r, dst));
        }
        else link (idPrefix + id, &dst, 0);
    }

    void i (const juce::String& id, const juce::String& name, int& dst, int lo, int hi)
    {
        if (layout) layout->add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { idPrefix + id, 1 }, namePrefix + name, lo, hi, dst));
        else link (idPrefix + id, &dst, 1);
    }

    void b (const juce::String& id, const juce::String& name, bool& dst)
    {
        if (layout) layout->add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { idPrefix + id, 1 }, namePrefix + name, dst));
        else link (idPrefix + id, &dst, 2);
    }

    void c (const juce::String& id, const juce::String& name, int& dst, const juce::StringArray& choices)
    {
        if (layout) layout->add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { idPrefix + id, 1 }, namePrefix + name, choices, dst));
        else link (idPrefix + id, &dst, 1);
    }

    // Copy the current host parameter values into target.
    void pull (EngineParams& target) const
    {
        for (const auto& e : entries)
        {
            char* d = reinterpret_cast<char*> (&target) + e.offset;
            float v = e.src->load (std::memory_order_relaxed);
            if (e.kind == 0) *reinterpret_cast<float*> (d) = v;
            else if (e.kind == 1) *reinterpret_cast<int*> (d) = (int) std::lround (v);
            else *reinterpret_cast<bool*> (d) = v > 0.5f;
        }
    }

    // Write every value of source into the host parameters (message thread).
    void push (const EngineParams& source) const
    {
        for (const auto& e : entries)
        {
            const char* s = reinterpret_cast<const char*> (&source) + e.offset;
            float v = e.kind == 0 ? *reinterpret_cast<const float*> (s)
                    : e.kind == 1 ? (float) *reinterpret_cast<const int*> (s)
                                  : (*reinterpret_cast<const bool*> (s) ? 1.0f : 0.0f);
            e.param->beginChangeGesture();
            e.param->setValueNotifyingHost (e.param->convertTo0to1 (v));
            e.param->endChangeGesture();
        }
    }

private:
    APVTS::ParameterLayout* layout = nullptr;
    APVTS* apvts = nullptr;
    EngineParams* baseObj = nullptr;
    juce::String idPrefix, namePrefix;
    std::vector<Entry> entries;

    template <typename T>
    void link (const juce::String& fullId, T* dst, int kind)
    {
        Entry e;
        e.src = apvts->getRawParameterValue (fullId);
        e.param = apvts->getParameter (fullId);
        e.offset = (size_t) (reinterpret_cast<char*> (dst) - reinterpret_cast<char*> (baseObj));
        e.kind = kind;
        jassert (e.src != nullptr && e.param != nullptr);
        if (e.src != nullptr && e.param != nullptr) entries.push_back (e);
    }
};

//==============================================================================
inline juce::StringArray makeChoices (int n, const char* (*nameFn) (int))
{
    juce::StringArray a;
    for (int k = 0; k < n; ++k) a.add (nameFn (k));
    return a;
}
inline const char* waveName (int i) { return WaveTables::name (i); }
inline const char* lfoWaveName (int i)
{
    static const char* n[7] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Glide Random" };
    return n[std::min (std::max (i, 0), 6)];
}
inline const char* mixOrderName (int i) { return i == 0 ? "Filter > Insert" : "Insert > Filter"; }

inline void describeLayer (ParamBinder& B, LayerParams& P, bool& solo, int layer)
{
    static const juce::StringArray tables = makeChoices (WaveTables::T, waveName);
    static const juce::StringArray oscFx = makeChoices (OfxNum, oscFxName);
    static const juce::StringArray fmTypes = makeChoices (2, fmTypeName);
    static const juce::StringArray fltTypes = makeChoices (FltNumTypes, filterTypeName);
    static const juce::StringArray insTypes = makeChoices (InsNumTypes, insertTypeName);
    static const juce::StringArray fxTypes = makeChoices (FxNumTypes, fxTypeName);
    static const juce::StringArray sources = makeChoices (SrcNum, modSourceName);
    static const juce::StringArray targets = makeChoices (TgNum, modTargetName);
    static const juce::StringArray divs = makeChoices (kNumDivs, divName);
    static const juce::StringArray divsRight = [] { juce::StringArray a; a.add ("Same as left"); a.addArray (makeChoices (kNumDivs, divName)); return a; }();
    static const juce::StringArray arpOrders = makeChoices (ArpNumOrders, arpOrderName);
    static const juce::StringArray lfoWaves = makeChoices (7, lfoWaveName);
    static const juce::StringArray mixOrders = makeChoices (2, mixOrderName);
    static const juce::StringArray scaleNames = makeChoices (kNumScales, scaleName);
    static const juce::StringArray keyNames = makeChoices (12, keyName);

    const juce::String L = "L" + juce::String (layer + 1);
    B.setPrefix (L + "_", L + " ");

    B.b ("on", "Enabled", P.enabled);
    B.b ("solo", "Solo", solo);
    B.f ("gain", "Gain dB", P.gainDb, -60.0f, 12.0f, -12.0f);
    B.f ("pan", "Pan", P.pan, -1.0f, 1.0f);

    const char* oscNames[3] = { "A", "B", "C" };
    for (int o = 0; o < kOscs; ++o)
    {
        OscParams& op = P.osc[o];
        const juce::String p = juce::String ("o") + oscNames[o] + "_", n = juce::String ("Osc ") + oscNames[o] + " ";
        B.setPrefix (L + "_" + p, L + " " + n);
        B.b ("on", "On", op.on);
        B.c ("tab", "Wavetable", op.table, tables);
        B.f ("morph", "Morph", op.morph, 0.0f, 1.0f);
        B.i ("semi", "Semi", op.semi, -48, 48);
        B.f ("cent", "Cent", op.cent, -100.0f, 100.0f);
        B.b ("key", "Key Track", op.keyTrack);
        B.b ("skip", "Skip To Mix", op.skipFilter);
        B.i ("uni", "Unison", op.unison, 1, kMaxUnison);
        B.f ("det", "Detune", op.detune, 0.0f, 1.0f);
        B.f ("spr", "Spread", op.spread, 0.0f, 1.0f);
        B.f ("vol", "Volume", op.volume, 0.0f, 1.5f);
        B.f ("bal", "Balance", op.balance, -1.0f, 1.0f);
        B.f ("pha", "Phase", op.phase, 0.0f, 1.0f);
        B.f ("rnd", "Random", op.random, 0.0f, 1.0f);
        B.f ("bass", "Bass", op.bass, -1.0f, 1.0f);
        B.f ("treb", "Treble", op.treble, -1.0f, 1.0f);
        B.c ("fx", "FX Type", op.fx, oscFx);
        B.f ("fxa", "FX Amount", op.fxAmt, 0.0f, 1.0f);
        B.f ("fxt", "FX Tune", op.fxTune, 0.0f, 1.0f);
        B.f ("fma", "FM From A", op.fmIn[0], -1.0f, 1.0f);
        B.f ("fmb", "FM From B", op.fmIn[1], -1.0f, 1.0f);
        B.f ("fmc", "FM From C", op.fmIn[2], -1.0f, 1.0f);
        B.c ("fmt", "FM Type", op.fmType, fmTypes);
    }

    B.setPrefix (L + "_fm_", L + " FM ");
    B.b ("on", "Mode", P.fmMode);

    B.setPrefix (L + "_f_", L + " Filter ");
    B.c ("type", "Type", P.filter.type, fltTypes);
    B.f ("cut", "Cutoff", P.filter.cutoff, 0.0f, 1.0f);
    B.f ("res", "Resonance", P.filter.resonance, 0.0f, 1.0f);
    B.f ("treb", "Treble", P.filter.treble, -1.0f, 1.0f);
    B.f ("env", "Envelope", P.filter.envAmount, -1.0f, 1.0f);
    B.f ("key", "Keyfollow", P.filter.keyFollow, -1.0f, 1.0f);
    B.f ("wid", "Width", P.filter.width, 0.0f, 1.0f);

    B.setPrefix (L + "_i_", L + " Insert ");
    B.c ("type", "Type", P.insert.type, insTypes);
    B.f ("drv", "Drive", P.insert.drive, 0.0f, 1.0f);
    B.f ("p2", "Control 2", P.insert.p2, 0.0f, 1.0f);
    B.f ("p3", "Control 3", P.insert.p3, 0.0f, 1.0f);
    B.f ("mix", "Mix", P.insert.mix, 0.0f, 1.0f);

    B.setPrefix (L + "_m_", L + " Mix ");
    B.c ("order", "Order", P.mixOrder, mixOrders);
    B.f ("gain", "Gain dB", P.mixGainDb, -24.0f, 12.0f);
    B.f ("pan", "Pan", P.mixPan, -1.0f, 1.0f);

    const char* letters[4] = { "A", "B", "C", "D" };
    for (int k = 0; k < kEnvs; ++k)
    {
        EnvParams& e = P.env[k];
        B.setPrefix (L + "_e" + letters[k] + "_", L + " Env " + letters[k] + " ");
        B.f ("vel", "Velocity", e.velocity, 0.0f, 1.0f);
        B.f ("dly", "Delay", e.delay, 0.0f, 4.0f, 0.3f);
        B.f ("atk", "Attack", e.attack, 0.0005f, 8.0f, 0.2f);
        B.f ("lvl", "Level", e.level, 0.0f, 1.0f);
        B.f ("dec", "Decay", e.decay, 0.001f, 8.0f, 0.4f);
        B.f ("sus", "Sustain", e.sustain, 0.0f, 1.0f);
        B.f ("rel", "Release", e.release, 0.001f, 10.0f, 0.5f);
        B.f ("sa", "Attack Slope", e.slopeA, 0.0f, 1.0f);
        B.f ("sd", "Decay Slope", e.slopeD, 0.0f, 1.0f);
        B.f ("sr", "Release Slope", e.slopeR, 0.0f, 1.0f);
    }
    for (int k = 0; k < kLfos; ++k)
    {
        LfoParams& l = P.lfo[k];
        B.setPrefix (L + "_l" + letters[k] + "_", L + " LFO " + letters[k] + " ");
        B.c ("wa", "Wave A", l.waveA, lfoWaves);
        B.c ("wb", "Wave B", l.waveB, lfoWaves);
        B.f ("xf", "X-Fade", l.xfade, 0.0f, 1.0f);
        B.f ("pha", "Phase", l.phase, 0.0f, 1.0f);
        B.f ("bnd", "Bend", l.bend, -1.0f, 1.0f);
        B.f ("rate", "Rate Hz", l.rateHz, 0.01f, 40.0f, 2.0f);
        B.f ("lvl", "Level", l.level, 0.0f, 1.0f);
        B.f ("dly", "Delay", l.delay, 0.0f, 4.0f, 0.3f);
        B.f ("fade", "Fade", l.fade, 0.0f, 4.0f, 0.3f);
        B.b ("sync", "BPM Sync", l.sync);
        B.c ("div", "Sync Division", l.syncDiv, divs);
        B.b ("rtg", "Retrigger", l.retrigger);
        B.b ("bip", "Bipolar", l.bipolar);
    }
    for (int k = 0; k < kSeqs; ++k)
    {
        SeqParams& s = P.seq[k];
        B.setPrefix (L + "_s" + letters[k] + "_", L + " Seq " + letters[k] + " ");
        B.i ("len", "Length", s.length, 2, kMaxSteps);
        B.i ("loop", "Loop", s.loopStart, 0, kMaxSteps - 1);
        B.f ("rate", "Rate", s.rateHz, 0.05f, 40.0f, 4.0f);
        B.b ("sync", "BPM Sync", s.sync);
        B.c ("div", "Sync Division", s.syncDiv, divs);
        B.b ("one", "Oneshot", s.oneshot);
        B.b ("rtg", "Retrigger", s.retrigger);
        B.f ("lvl", "Level", s.level, 0.0f, 1.0f);
        B.f ("smo", "Smooth", s.smooth, 0.0f, 1.0f);
        B.f ("shf", "Shuffle", s.shuffle, 0.0f, 1.0f);
        B.f ("gate", "Gate Length", s.gateLength, 0.05f, 1.0f);
        B.c ("scl", "Scale", s.scale, scaleNames);
        B.c ("key", "Key", s.key, keyNames);
    }
    for (int k = 0; k < kModSlots; ++k)
    {
        ModSlot& m = P.mod[k];
        B.setPrefix (L + "_r" + juce::String (k + 1) + "_", L + " Mod " + juce::String (k + 1) + " ");
        B.b ("on", "On", m.on);
        B.c ("src", "Source", m.source, sources);
        B.c ("tgt", "Target", m.target, targets);
        B.f ("amt", "Amount", m.amount, -1.0f, 1.0f);
    }

    B.setPrefix (L + "_v_", L + " Voicing ");
    B.i ("ch", "MIDI Channel", P.voicing.midiChannel, 0, 16);
    B.i ("lim", "Voice Limit", P.voicing.voiceLimit, 1, kMaxVoices);
    B.b ("mono", "Mono Legato", P.voicing.mono);
    B.b ("galw", "Glide Always", P.voicing.glideAlways);
    B.f ("glide", "Glide Time", P.voicing.glideTime, 0.0f, 2.0f, 0.3f);
    B.i ("lo", "Key Low", P.voicing.keyLow, 0, 127);
    B.i ("hi", "Key High", P.voicing.keyHigh, 0, 127);

    B.setPrefix (L + "_a_", L + " Arp ");
    B.b ("on", "On", P.arp.on);
    B.c ("ord", "Order", P.arp.order, arpOrders);
    B.i ("oct", "Octaves", P.arp.octaves, 1, 4);
    B.c ("div", "Rate", P.arp.div, divs);
    B.i ("len", "Length", P.arp.length, 1, kArpSteps);
    B.f ("gate", "Gate", P.arp.gateLength, 0.05f, 1.0f);
    B.f ("shf", "Shuffle", P.arp.shuffle, 0.0f, 1.0f);
    B.c ("scl", "Scale", P.arp.scale, scaleNames);
    B.c ("key", "Key", P.arp.key, keyNames);

    for (int k = 0; k < kFxSlots; ++k)
    {
        FxParams& x = P.fx[k];
        B.setPrefix (L + "_x" + juce::String (k + 1) + "_", L + " FX " + juce::String (k + 1) + " ");
        B.c ("type", "Type", x.type, fxTypes);
        B.f ("mix", "Mix", x.mix, 0.0f, 1.0f);
        B.f ("a", "Control 1", x.a, 0.0f, 1.0f);
        B.f ("b", "Control 2", x.b, 0.0f, 1.0f);
        B.f ("c", "Control 3", x.c, 0.0f, 1.0f);
        B.f ("d", "Control 4", x.d, 0.0f, 1.0f);
        B.c ("div", "Division", x.div, divs);
        B.c ("divr", "Delay Right Division", x.divR, divsRight);
        B.i ("len", "Length", x.gateLength, 1, kMaxSteps);
    }
}

inline void describeMaster (ParamBinder& B, EngineParams& E)
{
    B.setPrefix ("", "");
    B.f ("master_gain", "Master Gain dB", E.masterDb, -48.0f, 12.0f);
    B.b ("master_lim", "Limiter", E.limiterOn);
    B.f ("master_thr", "Limiter Threshold", E.limiterThreshold, 0.1f, 1.0f);
    B.f ("macro1", "Macro 1", E.macros[0], 0.0f, 1.0f);
    B.f ("macro2", "Macro 2", E.macros[1], 0.0f, 1.0f);
    B.f ("macro3", "Macro 3", E.macros[2], 0.0f, 1.0f);
    B.f ("pb_range", "Pitch Bend Range", E.pitchBendRange, 0.0f, 24.0f);
    B.f ("key_offset", "Key Offset", E.keyOffset, -24.0f, 24.0f);
}

inline void describeAll (ParamBinder& B, EngineParams& E)
{
    for (int l = 0; l < kLayers; ++l) describeLayer (B, E.layer[l], E.solo[l], l);
    describeMaster (B, E);
}

// Defaults: layer 1 is enabled with a simple saw patch, other layers are off but ready to enable.
inline EngineParams makeDefaultEngineParams()
{
    EngineParams E;
    for (int l = 0; l < kLayers; ++l)
    {
        LayerParams& P = E.layer[l];
        P.enabled = (l == 0);
        if (l == kLayers - 1)   // layer 8 is an FM layer: A is the carrier, B modulates it
        {
            P.fmMode = true;
            P.osc[0].unison = 1; P.osc[1].on = true; P.osc[1].unison = 1; P.osc[1].volume = 0.0f; P.osc[0].fmIn[1] = 0.35f;
            P.osc[1].semi = 12;
        }
        P.osc[0].on = true; if (l != kLayers - 1) { P.osc[0].unison = 3; } P.osc[0].detune = 0.3f;
        P.filter.cutoff = 0.62f; P.filter.resonance = 0.25f; P.filter.envAmount = 0.35f;
        P.env[1].decay = 0.5f; P.env[1].sustain = 0.3f;
        P.lfo[0].rateHz = 0.5f;
        P.mixGainDb = 0.0f;
        P.gainDb = -6.0f;
        P.seq[0].level = 1.0f;
        for (int k = 0; k < kSeqs; ++k) for (int s = 0; s < kMaxSteps; ++s) P.seq[k].steps[s] = (s % 4 == 0) ? 0.8f : (s % 2 == 0 ? 0.3f : -0.2f);
        for (int k = 0; k < kFxSlots; ++k) for (int s = 0; s < kMaxSteps; ++s) { P.fx[k].gate[0][s] = (float) (s % 2 == 0); P.fx[k].gate[1][s] = (float) (s % 4 != 3); }
        P.fx[0].type = FxReverb; P.fx[0].mix = 0.2f; P.fx[0].a = 0.6f; P.fx[0].b = 0.4f; P.fx[0].c = 0.8f; P.fx[0].d = 0.1f;
        P.fx[1].type = FxDelay; P.fx[1].mix = 0.0f; P.fx[1].a = 0.8f; P.fx[1].b = 0.4f; P.fx[1].c = 0.6f; P.fx[1].d = 0.5f;
    }
    E.masterDb = -3.0f;
    E.pitchBendRange = 2.0f;
    return E;
}
} // namespace stratum
