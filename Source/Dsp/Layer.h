#pragma once
// Stratum - one synthesis layer: 3 oscillators, filter, insert, 4 envelopes / LFOs / sequencers,
// 8-slot modulation matrix, arpeggiator and a 7-slot effect rack. No JUCE dependency.
#include "Common.h"
#include "Effects.h"
#include "Filter.h"
#include "Modulators.h"
#include "Scales.h"
#include "UserWavetable.h"
#include "Wavetable.h"
#include <vector>

namespace stratum
{
//==============================================================================
enum OscFx { OfxNone = 0, OfxTube, OfxPhaseMod, OfxRingMod, OfxHardSync, OfxBend, OfxMirror, OfxNoise, OfxChaos, OfxNum };

inline const char* oscFxName (int t)
{
    static const char* n[OfxNum] = { "None", "Tube", "Phase Mod", "Ring Mod", "Hard Sync", "Bend", "Mirror", "Noise", "Chaos Phase" };
    return n[std::min (std::max (t, 0), (int) OfxNum - 1)];
}

struct OscParams
{
    bool on = false;
    int table = 0;
    float morph = 0.0f;
    int semi = 0;
    float cent = 0.0f;
    bool keyTrack = true;
    bool skipFilter = false;
    int unison = 1;
    float detune = 0.25f, spread = 0.5f;
    float volume = 0.7f, balance = 0.0f;
    float phase = 0.0f, random = 0.0f;
    float bass = 0.0f, treble = 0.0f;
    int fx = OfxNone;
    float fxAmt = 0.0f, fxTune = 0.5f;
    const UserTable* user = nullptr;   // set by the host every block; when set it replaces the factory table
};

//==============================================================================
enum ModSource
{
    SrcNone = 0, SrcEnv1, SrcEnv2, SrcEnv3, SrcEnv4, SrcLfo1, SrcLfo2, SrcLfo3, SrcLfo4,
    SrcSeq1, SrcSeq2, SrcSeq3, SrcSeq4, SrcVelocity, SrcKeyFollow, SrcModWheel,
    SrcMacro1, SrcMacro2, SrcMacro3, SrcRandom, SrcNum
};

enum ModTarget
{
    TgNone = 0, TgOscVol1, TgOscVol2, TgOscVol3, TgOscMorph1, TgOscMorph2, TgOscMorph3,
    TgOscPitch1, TgOscPitch2, TgOscPitch3, TgCutoff, TgResonance, TgInsertDrive, TgLayerGain,
    TgLayerPan, TgDetune, TgFx1Mix, TgFx2Mix, TgFx3Mix, TgFx4Mix, TgFx5Mix, TgFx6Mix, TgFx7Mix, TgNum
};

inline const char* modSourceName (int s)
{
    static const char* n[SrcNum] = { "----", "Envelope A", "Envelope B", "Envelope C", "Envelope D",
        "LFO A", "LFO B", "LFO C", "LFO D", "Sequence A", "Sequence B", "Sequence C", "Sequence D",
        "Velocity", "Key Follow", "Mod-Wheel", "Macro 1", "Macro 2", "Macro 3", "Key Random" };
    return n[std::min (std::max (s, 0), (int) SrcNum - 1)];
}

inline const char* modTargetName (int t)
{
    static const char* n[TgNum] = { "----", "Osc-A Volume", "Osc-B Volume", "Osc-C Volume", "Osc-A Morph", "Osc-B Morph",
        "Osc-C Morph", "Osc-A Pitch", "Osc-B Pitch", "Osc-C Pitch", "Filter-Cutoff", "Filter-Resonance",
        "Insert-Drive", "Layer Gain", "Layer Pan", "Unison Detune", "FX 1 Mix", "FX 2 Mix", "FX 3 Mix",
        "FX 4 Mix", "FX 5 Mix", "FX 6 Mix", "FX 7 Mix" };
    return n[std::min (std::max (t, 0), (int) TgNum - 1)];
}

struct ModSlot { bool on = true; int source = SrcNone; int target = TgNone; float amount = 0.0f; };

//==============================================================================
struct VoicingParams
{
    int midiChannel = 0;        // 0 = all
    int voiceLimit = 8;
    bool mono = false;          // mono legato
    bool glideAlways = false;
    float glideTime = 0.0f;     // seconds
    int keyLow = 0, keyHigh = 127;
};

enum ArpOrder { ArpUp = 0, ArpDown, ArpUpDown, ArpDownUp, ArpQueue, ArpRandom, ArpNumOrders };

inline const char* arpOrderName (int o)
{
    static const char* n[ArpNumOrders] = { "Up", "Down", "Up Down", "Down Up", "Queue", "Random" };
    return n[std::min (std::max (o, 0), (int) ArpNumOrders - 1)];
}

struct ArpParams
{
    bool on = false;
    int order = ArpUp;
    int octaves = 1;
    int div = 6;            // index into kDivCyclesPerBeat, used as steps per beat
    int length = 16;
    float gateLength = 0.6f;
    float shuffle = 0.0f;
    int scale = 0, key = 0;   // scale lock (see Scales.h); 0 = off
    bool stepOn[kArpSteps];
    float tune[kArpSteps] = {};
    ArpParams() { for (auto& s : stepOn) s = true; }
};

struct LayerParams
{
    bool enabled = false;
    OscParams osc[kOscs];
    FilterParams filter;
    InsertParams insert;
    int mixOrder = 0;                 // 0 = filter then insert, 1 = insert then filter
    float mixGainDb = 0.0f;
    float mixPan = 0.0f;
    EnvParams env[kEnvs];
    LfoParams lfo[kLfos];
    SeqParams seq[kSeqs];
    ModSlot mod[kModSlots];
    VoicingParams voicing;
    ArpParams arp;
    FxParams fx[kFxSlots];
    float gainDb = 0.0f;              // layer fader
    float pan = 0.0f;
    bool toMaster = true;
};

struct LayerContext
{
    Transport transport;
    float macros[3] = { 0, 0, 0 };
    float modWheel = 0.0f;
    float pitchBendSemis = 0.0f;
    float masterTune = 0.0f;          // semitones, key offset
};

//==============================================================================
class LayerEngine
{
public:
    void prepare (double sr)
    {
        fs = (float) sr;
        for (auto& v : voices)
        {
            v.filter.prepare (sr);
            v.insert.prepare (sr);
            v.active = false;
        }
        for (auto& f : fx) f.prepare (sr);
        allNotesOff (true);
    }

    void noteOn (int note, float vel, const LayerParams& P)
    {
        if (P.arp.on)
        {
            arpHeld.erase (std::remove_if (arpHeld.begin(), arpHeld.end(), [note] (const Held& h) { return h.note == note; }), arpHeld.end());
            arpHeld.push_back ({ note, vel });
            if (arpHeld.size() == 1) { arpBeat = 1.0; arpCounter = 0; arpStep = 0; }   // fire first step right away
            return;
        }
        startNote (note, vel, P);
    }

    void noteOff (int note, const LayerParams& P)
    {
        if (P.arp.on || ! arpHeld.empty())
        {
            arpHeld.erase (std::remove_if (arpHeld.begin(), arpHeld.end(), [note] (const Held& h) { return h.note == note; }), arpHeld.end());
            if (arpHeld.empty()) stopArpNote();
            if (P.arp.on) return;
        }
        stopNote (note, P);
    }

    void allNotesOff (bool immediate = false)
    {
        arpHeld.clear(); arpNote = -1; monoStack.clear();
        for (auto& s : seqPos_) s.store (-1, std::memory_order_relaxed);
        arpPos_.store (-1, std::memory_order_relaxed);
        for (auto& v : voices)
        {
            if (immediate) v.active = false;
            else if (v.active && ! v.released) releaseVoice (v);
        }
    }

    int activeVoices() const { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

    LayerEngine() { for (auto& s : seqPos_) s.store (-1); arpPos_.store (-1); }

    // What the editor shows as the playhead: the step each sequence / the arpeggiator is on right now, or -1 when idle.
    int seqPosition (int k) const { return seqPos_[k].load (std::memory_order_relaxed); }
    int arpPosition() const { return arpPos_.load (std::memory_order_relaxed); }

    // Adds this layer's output into L/R (does not clear).
    void render (float* L, float* R, int n, const LayerParams& P, const LayerContext& ctx)
    {
        if (! P.arp.on && (arpNote >= 0 || ! arpHeld.empty())) { stopArpNote(); arpHeld.clear(); }
        if (scratchL.size() < (size_t) n) { scratchL.assign ((size_t) n, 0.0f); scratchR.assign ((size_t) n, 0.0f); }
        std::fill (scratchL.begin(), scratchL.begin() + n, 0.0f);
        std::fill (scratchR.begin(), scratchR.begin() + n, 0.0f);

        Transport tp = ctx.transport;
        for (int pos = 0; pos < n; pos += kCtlRate)
        {
            int m = std::min (kCtlRate, n - pos);
            tp.ppq = ctx.transport.ppq + (double) pos * ctx.transport.bpm / 60.0 / (double) fs;
            if (P.arp.on) arpTick (m, P, tp.bpm);
            float dt = (float) m / fs;
            for (int k = 0; k < kLfos; ++k) gLfoVal[k] = gLfo[k].advance (dt, P.lfo[k], tp.bpm);
            for (int k = 0; k < kSeqs; ++k) gSeqVal[k] = gSeq[k].advance (dt, P.seq[k], tp.bpm);
            for (auto& v : voices)
                if (v.active) renderVoice (v, scratchL.data() + pos, scratchR.data() + pos, m, P, ctx);
        }

        publishPositions (P);

        // effect rack
        Transport tpBlock = ctx.transport;
        for (int s = 0; s < kFxSlots; ++s)
            fx[s].process (scratchL.data(), scratchR.data(), n, P.fx[s], tpBlock);

        float g = dbToGain (P.gainDb);
        float pn = clampf (P.pan, -1.0f, 1.0f);
        float gl = g * std::cos ((pn + 1.0f) * kPi * 0.25f) * 1.41421356f;
        float gr = g * std::sin ((pn + 1.0f) * kPi * 0.25f) * 1.41421356f;
        for (int i = 0; i < n; ++i) { L[i] += scratchL[(size_t) i] * gl; R[i] += scratchR[(size_t) i] * gr; }
    }

private:
    struct Held { int note; float vel; };

    struct Voice
    {
        bool active = false, released = false;
        int note = 60;
        float noteF = 60.0f, vel = 1.0f, randVal = 0.0f, fadeIn = 0.0f;
        uint64_t age = 0;
        Envelope env[kEnvs];
        Lfo lfo[kLfos];
        StepSeq seq[kSeqs];
        float phase[kOscs][kMaxUnison] = {};
        float opPhase[kOscs][kMaxUnison] = {};
        float tone[kOscs][2][2] = {};
        float noiseLp[kOscs] = {};
        StereoFilter filter;
        StereoInsert insert;
        float prevAmp = 0.0f;
        Rng rng;

    };

    float fs = 44100.0f;
    Voice voices[kMaxVoices];
    FxSlot fx[kFxSlots];
    Lfo gLfo[kLfos];
    StepSeq gSeq[kSeqs];
    float gLfoVal[kLfos] = {}, gSeqVal[kSeqs] = {};
    std::atomic<int> seqPos_[kSeqs];
    std::atomic<int> arpPos_;
    std::vector<float> scratchL, scratchR;
    uint64_t ageCounter = 0;
    uint32_t seedCounter = 1;
    std::vector<Held> arpHeld;
    std::vector<int> monoStack;
    double arpBeat = 0.0;
    int arpCounter = 0, arpStep = 0, arpNote = -1;
    bool arpGateOpen = false;
    Rng arpRng;

    //--------------------------------------------------------------------------
    void releaseVoice (Voice& v)
    {
        v.released = true;
        for (auto& e : v.env) e.noteOff();
    }

    void initVoice (Voice& v, int note, float vel, const LayerParams& P)
    {
        v.active = true; v.released = false;
        v.note = note; v.noteF = (float) note; v.vel = vel;
        v.age = ++ageCounter;
        v.fadeIn = 0.0f; v.prevAmp = 0.0f;
        v.rng.s = (uint32_t) (note * 7919 + (int) (ageCounter * 104729u));
        v.randVal = v.rng.next();
        uint32_t seed = ++seedCounter;
        for (int k = 0; k < kEnvs; ++k) v.env[k].noteOn (vel);
        for (int k = 0; k < kLfos; ++k) v.lfo[k].reset (seed + (uint32_t) k * 31u);
        for (int k = 0; k < kSeqs; ++k) v.seq[k].reset();
        v.filter.reset(); v.insert.reset();
        for (int o = 0; o < kOscs; ++o)
        {
            for (int u = 0; u < kMaxUnison; ++u)
            {
                float rnd = P.osc[o].random * v.rng.next01();
                v.phase[o][u] = std::fmod (P.osc[o].phase + rnd + (P.osc[o].random > 0.0f ? (float) u * 0.137f * P.osc[o].random : 0.0f), 1.0f);
                v.opPhase[o][u] = 0.0f;
            }
            for (auto& c : v.tone[o]) c[0] = c[1] = 0.0f;
        }
    }

    void startNote (int note, float vel, const LayerParams& P)
    {
        const VoicingParams& vp = P.voicing;
        if (vp.mono)
        {
            monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
            monoStack.push_back (note);
            for (auto& v : voices)
                if (v.active && ! v.released)
                {
                    v.note = note;               // legato: keep envelopes running, glide pitch
                    v.vel = vel;
                    return;
                }
            Voice& v = pickVoice (1);
            initVoice (v, note, vel, P);
            return;
        }
        Voice& v = pickVoice (std::max (1, std::min (vp.voiceLimit, kMaxVoices)));
        initVoice (v, note, vel, P);
        if (vp.glideAlways && vp.glideTime > 0.0f)
            for (auto& o : voices) if (&o != &v && o.active && ! o.released) { v.noteF = o.noteF; break; }
    }

    void stopNote (int note, const LayerParams& P)
    {
        if (P.voicing.mono)
        {
            monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
            for (auto& v : voices)
                if (v.active && ! v.released)
                {
                    if (! monoStack.empty()) v.note = monoStack.back();
                    else releaseVoice (v);
                    return;
                }
            return;
        }
        for (auto& v : voices)
            if (v.active && ! v.released && v.note == note) { releaseVoice (v); break; }
    }

    Voice& pickVoice (int limit)
    {
        int count = 0;
        Voice* oldest = nullptr; Voice* oldestReleased = nullptr; Voice* freeV = nullptr;
        for (auto& v : voices)
        {
            if (! v.active) { if (! freeV) freeV = &v; continue; }
            ++count;
            if (! oldest || v.age < oldest->age) oldest = &v;
            if (v.released && (! oldestReleased || v.age < oldestReleased->age)) oldestReleased = &v;
        }
        if (freeV && count < limit) return *freeV;
        if (oldestReleased) return *oldestReleased;
        return oldest ? *oldest : voices[0];
    }

    //--------------------------------------------------------------------------
    // Sequences follow the newest sounding note, or the shared clock when they are not set to retrigger.
    void publishPositions (const LayerParams& P)
    {
        const Voice* newest = nullptr;
        for (const auto& v : voices)
            if (v.active && (newest == nullptr || v.age > newest->age)) newest = &v;
        for (int k = 0; k < kSeqs; ++k)
        {
            int step = -1;
            if (newest != nullptr) step = P.seq[k].retrigger ? newest->seq[k].stepIndex (P.seq[k]) : gSeq[k].stepIndex (P.seq[k]);
            seqPos_[k].store (step, std::memory_order_relaxed);
        }
        if (! P.arp.on || arpHeld.empty()) arpPos_.store (-1, std::memory_order_relaxed);
    }

    void stopArpNote()
    {
        if (arpNote >= 0)
        {
            for (auto& v : voices)
                if (v.active && ! v.released && v.note == arpNote) releaseVoice (v);
            arpNote = -1;
        }
        arpGateOpen = false;
    }

    void arpTick (int m, const LayerParams& P, double bpm)
    {
        const ArpParams& A = P.arp;
        if (arpHeld.empty()) return;
        double stepsPerBeat = (double) kDivCyclesPerBeat[std::min (std::max (A.div, 0), kNumDivs - 1)];
        double inc = (double) m / (double) fs * bpm / 60.0 * stepsPerBeat;
        arpBeat += inc;
        int len = std::max (1, std::min (A.length, kArpSteps));

        if (arpGateOpen && arpBeat >= (double) A.gateLength && arpBeat < 1.0) { stopArpNote(); }
        if (arpBeat < 1.0) return;
        arpBeat -= std::floor (arpBeat);
        stopArpNote();

        int s = arpStep % len;
        arpStep = (arpStep + 1) % len;
        arpPos_.store (s, std::memory_order_relaxed);   // shown even when the step itself is switched off
        if (! A.stepOn[s]) return;

        std::vector<Held> pool = arpHeld;
        if (A.order != ArpQueue) std::sort (pool.begin(), pool.end(), [] (const Held& a, const Held& b) { return a.note < b.note; });
        int oct = std::max (1, std::min (A.octaves, 4));
        std::vector<Held> full;
        for (int o = 0; o < oct; ++o) for (auto& h : pool) full.push_back ({ h.note + 12 * o, h.vel });
        int n = (int) full.size();
        int idx = 0;
        switch (A.order)
        {
            case ArpUp: case ArpQueue: idx = arpCounter % n; break;
            case ArpDown: idx = n - 1 - (arpCounter % n); break;
            case ArpUpDown: { int cyc = std::max (1, 2 * n - 2); int k = arpCounter % cyc; idx = k < n ? k : cyc - k; break; }
            case ArpDownUp: { int cyc = std::max (1, 2 * n - 2); int k = arpCounter % cyc; idx = n - 1 - (k < n ? k : cyc - k); break; }
            default: idx = (int) (arpRng.next01() * (float) n) % n; break;
        }
        ++arpCounter;
        const float wanted = quantizeToScale ((float) full[(size_t) idx].note + A.tune[s], A.scale, A.key);
        int note = (int) std::lround (clampf (wanted, 0.0f, 127.0f));
        arpNote = note;
        arpGateOpen = true;
        startNote (note, full[(size_t) idx].vel, P);
    }

    //--------------------------------------------------------------------------
    static float frac (float x) { return x - std::floor (x); }

    void renderVoice (Voice& v, float* L, float* R, int m, const LayerParams& P, const LayerContext& ctx)
    {
        const float dt = (float) m / fs;
        const double bpm = ctx.transport.bpm;

        // glide
        float target = (float) v.note;
        bool glide = P.voicing.glideTime > 0.001f && (P.voicing.mono || P.voicing.glideAlways);
        if (glide) v.noteF += (target - v.noteF) * (1.0f - std::exp (-dt / (P.voicing.glideTime * 0.35f)));
        else v.noteF = target;

        // modulation sources
        float src[SrcNum] = {};
        for (int k = 0; k < kEnvs; ++k) src[SrcEnv1 + k] = v.env[k].advance (dt, P.env[k]);
        for (int k = 0; k < kLfos; ++k)
            src[SrcLfo1 + k] = P.lfo[k].retrigger ? v.lfo[k].advance (dt, P.lfo[k], bpm) : gLfoVal[k];
        for (int k = 0; k < kSeqs; ++k)
            src[SrcSeq1 + k] = P.seq[k].retrigger ? v.seq[k].advance (dt, P.seq[k], bpm) : gSeqVal[k];
        src[SrcVelocity] = v.vel;
        src[SrcKeyFollow] = (v.noteF - 60.0f) / 60.0f;
        src[SrcModWheel] = ctx.modWheel;
        for (int k = 0; k < 3; ++k) src[SrcMacro1 + k] = ctx.macros[k];
        src[SrcRandom] = v.randVal;

        float tg[TgNum] = {};
        for (const auto& slot : P.mod)
            if (slot.on && slot.source != SrcNone && slot.target != TgNone)
            {
                float amount = src[slot.source] * slot.amount;
                // A sequence with a scale chosen snaps the pitch it produces to that scale (1.0 = 24 semitones).
                if (slot.source >= SrcSeq1 && slot.source <= SrcSeq4 && slot.target >= TgOscPitch1 && slot.target <= TgOscPitch3)
                {
                    const SeqParams& sq = P.seq[slot.source - SrcSeq1];
                    if (sq.scale > 0) amount = (quantizeToScale (v.noteF + amount * 24.0f, sq.scale, sq.key) - v.noteF) / 24.0f;
                }
                tg[slot.target] += amount;
            }

        // retire finished voices
        if (v.released && ! v.env[0].active()) { v.active = false; return; }

        // filter
        FilterParams fp = P.filter;
        float cutoff = fp.cutoff + fp.envAmount * src[SrcEnv2] + fp.keyFollow * src[SrcKeyFollow] + tg[TgCutoff];
        fp.resonance = clampf (fp.resonance + tg[TgResonance], 0.0f, 1.0f);
        v.filter.setup (fp, cutoffToHz (clampf (cutoff, 0.0f, 1.0f)));

        InsertParams ip = P.insert;
        ip.drive = clampf (ip.drive + tg[TgInsertDrive], 0.0f, 1.0f);
        float noteHz = midiToHz (v.noteF + ctx.masterTune);

        // amplitude, pan
        float amp = src[SrcEnv1] * std::max (0.0f, 1.0f + tg[TgLayerGain]) * dbToGain (P.mixGainDb);
        float vpan = clampf (P.mixPan + tg[TgLayerPan], -1.0f, 1.0f);
        float pgL = std::cos ((vpan + 1.0f) * kPi * 0.25f) * 1.41421356f;
        float pgR = std::sin ((vpan + 1.0f) * kPi * 0.25f) * 1.41421356f;
        float ampStart = v.prevAmp, ampEnd = amp;
        v.prevAmp = amp;

        // per-oscillator chunk setup
        struct OscCtx
        {
            bool on; int table, mip, nu; const UserTable* user; float morph, vol, freq, detuneCents, norm, tubeNorm;
            float inc[kMaxUnison], gl[kMaxUnison], gr[kMaxUnison];
        };
        OscCtx oc[kOscs];
        const WaveTables& wt = WaveTables::get();
        for (int o = 0; o < kOscs; ++o)
        {
            const OscParams& op = P.osc[o];
            oc[o].on = op.on;
            if (! op.on) continue;
            float base = op.keyTrack ? v.noteF + ctx.masterTune + ctx.pitchBendSemis : 60.0f;
            float semis = base + (float) op.semi + op.cent * 0.01f + tg[TgOscPitch1 + o] * 24.0f;
            oc[o].freq = midiToHz (semis);
            oc[o].table = op.table;
            oc[o].morph = clampf (op.morph + tg[TgOscMorph1 + o], 0.0f, 1.0f);
            oc[o].vol = clampf (op.volume + tg[TgOscVol1 + o], 0.0f, 1.5f);
            oc[o].detuneCents = 75.0f * clampf (op.detune + tg[TgDetune], 0.0f, 1.0f);
            const float mipFreq = oc[o].freq * (1.0f + oc[o].detuneCents * 0.0006f) * (op.fx == OfxHardSync ? 1.0f + op.fxAmt * 7.0f : 1.0f);
            oc[o].user = op.user;
            oc[o].mip = op.user != nullptr ? UserTable::mipFor (mipFreq, fs) : WaveTables::mipFor (mipFreq, fs);
            oc[o].nu = std::max (1, std::min (op.unison, kMaxUnison));
            oc[o].norm = 1.0f / std::sqrt ((float) oc[o].nu);
            oc[o].tubeNorm = 1.0f / std::tanh (1.0f + op.fxAmt * 3.0f);
            for (int u = 0; u < oc[o].nu; ++u)
            {
                float off = oc[o].nu == 1 ? 0.0f : ((float) u / (float) (oc[o].nu - 1)) * 2.0f - 1.0f;
                oc[o].inc[u] = oc[o].freq * std::exp2 (off * oc[o].detuneCents / 1200.0f) * (1.0f / fs);
                float pn = clampf (off * op.spread + op.balance, -1.0f, 1.0f);
                oc[o].gl[u] = std::cos ((pn + 1.0f) * kPi * 0.25f) * 1.41421356f * oc[o].norm;
                oc[o].gr[u] = std::sin ((pn + 1.0f) * kPi * 0.25f) * 1.41421356f * oc[o].norm;
            }
        }

        const float fadeStep = 1.0f / (0.002f * fs);
        for (int i = 0; i < m; ++i)
        {
            float mixL = 0.0f, mixR = 0.0f, byL = 0.0f, byR = 0.0f;
            for (int o = 0; o < kOscs; ++o)
            {
                if (! oc[o].on) continue;
                const OscParams& op = P.osc[o];
                const int nu = oc[o].nu;
                float uL = 0.0f, uR = 0.0f;
                for (int u = 0; u < nu; ++u)
                {
                    const float inc = oc[o].inc[u];
                    float& ph = v.phase[o][u];
                    ph += inc; if (ph >= 1.0f) ph -= std::floor (ph);
                    float p = ph;
                    float& opp = v.opPhase[o][u];
                    float x;
                    switch (op.fx)
                    {
                        case OfxPhaseMod:
                            opp = frac (opp + inc * std::exp2 ((op.fxTune - 0.5f) * 4.0f));
                            p = frac (p + op.fxAmt * 0.6f * std::sin (kTwoPi * opp));
                            break;
                        case OfxHardSync: p = frac (p * (1.0f + op.fxAmt * 7.0f)); break;
                        case OfxBend: p = std::pow (p, std::pow (4.0f, (op.fxAmt - 0.5f) * 2.0f)); break;
                        case OfxMirror:
                        {
                            float mp = p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
                            p = std::pow (mp, std::pow (3.0f, (op.fxTune - 0.5f) * 2.0f * op.fxAmt));
                            break;
                        }
                        case OfxChaos: p = frac (p + v.rng.next() * op.fxAmt * 0.04f + 1.0f); break;
                        case OfxRingMod: opp = frac (opp + inc * std::exp2 ((op.fxTune - 0.5f) * 6.0f + 1.0f)); break;
                        default: break;
                    }
                    x = oc[o].user != nullptr ? oc[o].user->read (oc[o].mip, oc[o].morph, p)
                                              : wt.read (oc[o].table, oc[o].mip, oc[o].morph, p);
                    switch (op.fx)
                    {
                        case OfxTube: x = std::tanh (x * (1.0f + op.fxAmt * 10.0f)) * oc[o].tubeNorm; break;
                        case OfxRingMod: x *= lerpf (1.0f, std::sin (kTwoPi * opp), op.fxAmt); break;
                        case OfxNoise:
                        {
                            float nz = v.rng.next();
                            v.noiseLp[o] += lerpf (0.04f, 1.0f, op.fxTune) * (nz - v.noiseLp[o]);
                            x = lerpf (x, v.noiseLp[o] * 2.0f, op.fxAmt);
                            break;
                        }
                        default: break;
                    }
                    uL += x * oc[o].gl[u];
                    uR += x * oc[o].gr[u];
                }
                if (op.bass != 0.0f || op.treble != 0.0f)
                {
                    float* tl = v.tone[o][0]; float* tr = v.tone[o][1];
                    tl[0] += 0.028f * (uL - tl[0]); tl[1] += 0.35f * (uL - tl[1]);
                    tr[0] += 0.028f * (uR - tr[0]); tr[1] += 0.35f * (uR - tr[1]);
                    uL += op.bass * tl[0] * 1.5f + op.treble * (uL - tl[1]) * 1.5f;
                    uR += op.bass * tr[0] * 1.5f + op.treble * (uR - tr[1]) * 1.5f;
                }
                uL *= oc[o].vol; uR *= oc[o].vol;
                if (op.skipFilter) { byL += uL; byR += uR; } else { mixL += uL; mixR += uR; }
            }

            if (P.mixOrder == 0) { v.filter.process (mixL, mixR); v.insert.process (mixL, mixR, ip, noteHz); }
            else { v.insert.process (mixL, mixR, ip, noteHz); v.filter.process (mixL, mixR); }
            mixL += byL; mixR += byR;

            v.fadeIn = std::min (1.0f, v.fadeIn + fadeStep);
            float a = lerpf (ampStart, ampEnd, (float) (i + 1) / (float) m) * v.fadeIn;
            L[i] += mixL * a * pgL * 0.35f;
            R[i] += mixR * a * pgR * 0.35f;
        }
    }
};

//==============================================================================
struct EngineParams
{
    LayerParams layer[kLayers];
    float layerMuteSolo[kLayers] = { 0 };   // unused placeholder for future use
    bool solo[kLayers] = {};
    float masterDb = 0.0f;
    bool limiterOn = true;
    float limiterThreshold = 0.9f;
    float macros[3] = { 0, 0, 0 };
    float modWheel = 0.0f;
    float pitchBend = 0.0f;          // -1..1
    float pitchBendRange = 2.0f;     // semitones
    float keyOffset = 0.0f;          // semitones
};

class Engine
{
public:
    void prepare (double sr, int maxBlock)
    {
        fs = sr;
        for (auto& l : layers) l.prepare (sr);
        for (auto& b : bufL) b.assign ((size_t) maxBlock, 0.0f);
        for (auto& b : bufR) b.assign ((size_t) maxBlock, 0.0f);
        limGain = 1.0f;
    }

    bool layerAccepts (const LayerParams& P, int channel, int note) const
    {
        if (! P.enabled) return false;
        if (note < P.voicing.keyLow || note > P.voicing.keyHigh) return false;
        return P.voicing.midiChannel == 0 || P.voicing.midiChannel == channel;
    }

    void noteOn (int channel, int note, float vel, const EngineParams& E)
    {
        for (int l = 0; l < kLayers; ++l)
            if (layerAccepts (E.layer[l], channel, note) && layerEnabled (E, l)) layers[l].noteOn (note, vel, E.layer[l]);
    }
    void noteOff (int channel, int note, const EngineParams& E)
    {
        for (int l = 0; l < kLayers; ++l)
            if (E.layer[l].voicing.midiChannel == 0 || E.layer[l].voicing.midiChannel == channel)
                layers[l].noteOff (note, E.layer[l]);
    }
    void allNotesOff() { for (auto& l : layers) l.allNotesOff (false); }
    void panic() { for (auto& l : layers) l.allNotesOff (true); }

    int totalVoices() const { int c = 0; for (auto& l : layers) c += l.activeVoices(); return c; }
    int seqPosition (int layer, int seq) const { return layers[layer].seqPosition (seq); }
    int arpPosition (int layer) const { return layers[layer].arpPosition(); }
    float layerPeak (int l) const { return peaks[l]; }
    float outPeak() const { return outPk; }

    void render (float* L, float* R, int n, const EngineParams& E, const Transport& tp)
    {
        std::fill (L, L + n, 0.0f);
        std::fill (R, R + n, 0.0f);
        LayerContext ctx;
        ctx.transport = tp;
        for (int k = 0; k < 3; ++k) ctx.macros[k] = E.macros[k];
        ctx.modWheel = E.modWheel;
        ctx.pitchBendSemis = E.pitchBend * E.pitchBendRange;
        ctx.masterTune = E.keyOffset;

        for (int l = 0; l < kLayers; ++l)
        {
            if (! E.layer[l].enabled)
            {
                if (wasEnabled[l]) layers[l].allNotesOff (true);
                wasEnabled[l] = false; peaks[l] = 0.0f;
                continue;
            }
            wasEnabled[l] = true;
            std::fill (bufL[l].begin(), bufL[l].begin() + n, 0.0f);
            std::fill (bufR[l].begin(), bufR[l].begin() + n, 0.0f);
            layers[l].render (bufL[l].data(), bufR[l].data(), n, E.layer[l], ctx);
            float pk = 0.0f;
            for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::fabs (bufL[l][(size_t) i]), std::fabs (bufR[l][(size_t) i])));
            peaks[l] = std::max (pk, peaks[l] * 0.92f);
            if (E.layer[l].toMaster && layerEnabled (E, l))
                for (int i = 0; i < n; ++i) { L[i] += bufL[l][(size_t) i]; R[i] += bufR[l][(size_t) i]; }
        }

        float mg = dbToGain (E.masterDb);
        float thr = clampf (E.limiterThreshold, 0.05f, 1.0f);
        float rel = std::exp (-1.0f / ((float) fs * 0.08f));
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i] * mg, r = R[i] * mg;
            if (E.limiterOn)
            {
                float lvl = std::max (std::fabs (l), std::fabs (r));
                float want = lvl > thr ? thr / lvl : 1.0f;
                limGain = want < limGain ? want : want + (limGain - want) * rel;
                l *= limGain; r *= limGain;
                l = clampf (l, -1.0f, 1.0f); r = clampf (r, -1.0f, 1.0f);
            }
            L[i] = l; R[i] = r;
            pk = std::max (pk, std::max (std::fabs (l), std::fabs (r)));
        }
        outPk = std::max (pk, outPk * 0.9f);
    }

private:
    double fs = 44100.0;
    LayerEngine layers[kLayers];
    std::vector<float> bufL[kLayers], bufR[kLayers];
    float peaks[kLayers] = {};
    bool wasEnabled[kLayers] = {};
    float outPk = 0.0f, limGain = 1.0f;

    bool layerEnabled (const EngineParams& E, int l) const
    {
        bool anySolo = false;
        for (int i = 0; i < kLayers; ++i) anySolo = anySolo || E.solo[i];
        return ! anySolo || E.solo[l];
    }
};
} // namespace stratum
