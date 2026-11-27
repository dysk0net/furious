// Stratum DSP self-test. Builds without JUCE:
//   g++ -O2 -std=c++17 -ISource/Dsp Tests/dsp_test.cpp -o dsp_test && ./dsp_test
// Renders audio for every filter / insert / oscillator-FX / effect type and checks for NaN, Inf and runaway levels.
#include "../Source/Dsp/Layer.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <set>
#include <vector>

using namespace stratum;

struct Stats { double rms = 0, peak = 0; long bad = 0; double dc = 0; };

static Stats renderSeconds (Engine& eng, EngineParams& E, float seconds, double fs, std::vector<float>* outWav = nullptr)
{
    Stats s; long total = 0;
    std::vector<float> L (512), R (512);
    Transport tp; tp.bpm = 120.0; tp.ppq = 0.0;
    int blocks = (int) (seconds * fs / 512.0);
    for (int b = 0; b < blocks; ++b)
    {
        eng.render (L.data(), R.data(), 512, E, tp);
        tp.ppq += 512.0 * tp.bpm / 60.0 / fs;
        for (int i = 0; i < 512; ++i)
        {
            float l = L[(size_t) i], r = R[(size_t) i];
            if (! std::isfinite (l) || ! std::isfinite (r) || std::fabs (l) > 4.0f || std::fabs (r) > 4.0f) ++s.bad;
            s.rms += (double) l * l + (double) r * r; s.dc += l + r;
            s.peak = std::max (s.peak, (double) std::max (std::fabs (l), std::fabs (r)));
            total += 2;
            if (outWav) { outWav->push_back (l); outWav->push_back (r); }
        }
    }
    s.rms = std::sqrt (s.rms / (double) std::max (total, 1L));
    s.dc /= (double) std::max (total, 1L);
    return s;
}

static void writeWav (const char* path, const std::vector<float>& d, int fs)
{
    FILE* f = std::fopen (path, "wb"); if (! f) return;
    uint32_t dataBytes = (uint32_t) d.size() * 2, riff = 36 + dataBytes, sr = (uint32_t) fs, br = sr * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bps = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmt, 4, 1, f); std::fwrite (&pcm, 2, 1, f); std::fwrite (&ch, 2, 1, f); std::fwrite (&sr, 4, 1, f);
    std::fwrite (&br, 4, 1, f); std::fwrite (&ba, 2, 1, f); std::fwrite (&bps, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (float v : d) { int16_t s = (int16_t) (clampf (v, -1.0f, 1.0f) * 32767.0f); std::fwrite (&s, 2, 1, f); }
    std::fclose (f);
}

static void initPatch (EngineParams& E)
{
    for (int l = 1; l < kLayers; ++l) E.layer[l].enabled = false;
    LayerParams& P = E.layer[0];
    P.enabled = true;
    P.voicing = VoicingParams(); P.arp = ArpParams();
    P.osc[0].on = true; P.osc[0].table = 0; P.osc[0].unison = 3; P.osc[0].detune = 0.3f; P.osc[0].spread = 0.7f;
    P.osc[1].on = true; P.osc[1].table = 3; P.osc[1].semi = -12; P.osc[1].volume = 0.5f; P.osc[1].unison = 2;
    P.filter.type = FltLP24; P.filter.cutoff = 0.55f; P.filter.resonance = 0.35f; P.filter.envAmount = 0.4f;
    P.env[1].decay = 0.6f; P.env[1].sustain = 0.2f;
    P.mod[0] = { true, SrcLfo1, TgOscMorph1, 0.5f };
    P.lfo[0].rateHz = 0.8f;
    P.fx[0].type = FxHallReverb; P.fx[0].mix = 0.25f;
}

#define CHECK(cond, msg) do { if (! (cond)) { std::printf ("FAIL: %s\n", msg); ++failures; } else std::printf ("ok:   %s\n", msg); } while (0)

int main()
{
    const double fs = 44100.0;
    int failures = 0;
    std::unique_ptr<Engine> eng (new Engine());
    std::unique_ptr<EngineParams> E (new EngineParams());
    eng->prepare (fs, 512);

    // 0. dotted divisions are 1.5x the length of their straight counterparts (1/2 .. 1/32 are entries 3..7)
    {
        bool ok = true;
        for (int k = 0; k < 5; ++k) ok = ok && std::fabs (kDivCyclesPerBeat[9 + k] * 1.5f - kDivCyclesPerBeat[3 + k]) < 1.0e-4f;
        CHECK (ok && kNumDivs == 14, "dotted tempo divisions are 1.5x as long as straight ones");
    }

    // 1. basic patch
    initPatch (*E);
    eng->noteOn (1, 48, 0.9f, *E); eng->noteOn (1, 55, 0.9f, *E); eng->noteOn (1, 64, 0.9f, *E);
    std::vector<float> wav;
    Stats s = renderSeconds (*eng, *E, 2.0f, fs, &wav);
    std::printf ("basic chord: rms %.4f peak %.4f dc %.5f bad %ld\n", s.rms, s.peak, s.dc, s.bad);
    CHECK (s.bad == 0, "basic chord has no NaN/Inf/overflow");
    CHECK (s.rms > 0.01 && s.peak < 1.01, "basic chord is audible and limited");
    CHECK (std::fabs (s.dc) < 0.02, "basic chord has small DC offset");
    eng->noteOff (1, 48, *E); eng->noteOff (1, 55, *E); eng->noteOff (1, 64, *E);
    s = renderSeconds (*eng, *E, 3.0f, fs, &wav);
    s = renderSeconds (*eng, *E, 0.5f, fs);
    CHECK (s.rms < 0.005, "voices release to silence");
    CHECK (eng->totalVoices() == 0, "all voices freed after release");
    writeWav ("/tmp/stratum_chord.wav", wav, (int) fs);

    // 2. every filter type
    for (int t = 0; t < FltNumTypes; ++t)
    {
        initPatch (*E); E->layer[0].filter.type = t; E->layer[0].filter.resonance = 0.9f; E->layer[0].filter.cutoff = 0.4f;
        eng->panic (); eng->noteOn (1, 40, 1.0f, *E);
        s = renderSeconds (*eng, *E, 0.6f, fs);
        char msg[96]; std::snprintf (msg, sizeof msg, "filter %-8s stable (rms %.3f peak %.3f)", filterTypeName (t), s.rms, s.peak);
        CHECK (s.bad == 0, msg);
    }

    // 3. every insert type
    for (int t = 0; t < InsNumTypes; ++t)
    {
        initPatch (*E); E->layer[0].insert.type = t; E->layer[0].insert.drive = 0.9f;
        eng->panic (); eng->noteOn (1, 52, 1.0f, *E);
        s = renderSeconds (*eng, *E, 0.4f, fs);
        char msg[96]; std::snprintf (msg, sizeof msg, "insert %-16s stable (rms %.3f)", insertTypeName (t), s.rms);
        CHECK (s.bad == 0, msg);
    }

    // 4. every oscillator fx
    for (int t = 0; t < OfxNum; ++t)
    {
        initPatch (*E); E->layer[0].osc[0].fx = t; E->layer[0].osc[0].fxAmt = 0.8f; E->layer[0].osc[1].fx = t; E->layer[0].osc[1].fxAmt = 0.5f;
        eng->panic (); eng->noteOn (1, 60, 1.0f, *E);
        s = renderSeconds (*eng, *E, 0.4f, fs);
        char msg[96]; std::snprintf (msg, sizeof msg, "osc fx %-12s stable (rms %.3f)", oscFxName (t), s.rms);
        CHECK (s.bad == 0 && s.rms > 0.001, msg);
    }

    // 5. every wavetable at several pitches (alias / bounds check)
    for (int t = 0; t < WaveTables::T; ++t)
    {
        initPatch (*E); E->layer[0].osc[0].table = t; E->layer[0].osc[1].on = false; E->layer[0].osc[0].morph = 0.7f;
        eng->panic ();
        for (int n : { 24, 60, 96, 120 }) eng->noteOn (1, n, 1.0f, *E);
        s = renderSeconds (*eng, *E, 0.4f, fs);
        char msg[96]; std::snprintf (msg, sizeof msg, "wavetable %-12s stable (peak %.3f)", WaveTables::name (t), s.peak);
        CHECK (s.bad == 0 && s.rms > 0.001, msg);
    }

    // 6. every effect type, in all 7 slots at once
    for (int t = 1; t < FxNumTypes; ++t)
    {
        initPatch (*E);
        for (int k = 0; k < kFxSlots; ++k) { E->layer[0].fx[k].type = t; E->layer[0].fx[k].mix = 0.5f; }
        eng->panic (); eng->noteOn (1, 57, 1.0f, *E);
        s = renderSeconds (*eng, *E, 0.8f, fs);
        char msg[96]; std::snprintf (msg, sizeof msg, "effect %-11s x7 stable (rms %.3f peak %.3f)", fxTypeName (t), s.rms, s.peak);
        CHECK (s.bad == 0, msg);
    }

    // 7. modulation matrix: every source to every target
    initPatch (*E);
    E->layer[0].seq[0].sync = false; E->layer[0].seq[0].rateHz = 6.0f;
    for (int st = 0; st < kMaxSteps; ++st) E->layer[0].seq[0].steps[st] = (st % 4) * 0.5f - 0.5f;
    for (int src = 1; src < SrcNum; ++src)
        for (int tgt = 1; tgt < TgNum; ++tgt)
        {
            E->layer[0].mod[0] = { true, src, tgt, 1.0f };
            E->layer[0].mod[1] = { true, src, tgt, -1.0f };
            eng->panic (); eng->noteOn (1, 60, 0.8f, *E);
            s = renderSeconds (*eng, *E, 0.05f, fs);
            if (s.bad) { std::printf ("FAIL: mod %s -> %s unstable\n", modSourceName (src), modTargetName (tgt)); ++failures; }
        }
    std::printf ("ok:   modulation matrix sweep complete\n");

    // 8. arpeggiator
    initPatch (*E); eng->panic ();
    E->layer[0].arp.on = true; E->layer[0].arp.order = ArpUpDown; E->layer[0].arp.octaves = 2; E->layer[0].arp.div = 6;
    eng->noteOn (1, 48, 1.0f, *E); eng->noteOn (1, 52, 1.0f, *E); eng->noteOn (1, 55, 1.0f, *E);
    int maxV = 0; bool sawTwoDistinct = false;
    {
        std::vector<float> L (512), R (512); Transport tp; tp.bpm = 140; int last = -1, changes = 0;
        for (int b = 0; b < 200; ++b)
        {
            eng->render (L.data(), R.data(), 512, *E, tp); tp.ppq += 512.0 * tp.bpm / 60.0 / fs;
            maxV = std::max (maxV, eng->totalVoices());
            if (eng->totalVoices() != last) { ++changes; last = eng->totalVoices(); }
        }
        sawTwoDistinct = changes > 8;
    }
    CHECK (sawTwoDistinct, "arpeggiator triggers repeated notes");
    std::printf ("      (arp peak voice count %d)\n", maxV);
    CHECK (maxV <= 6, "arpeggiator plays one note at a time (plus releasing tails)");
    E->layer[0].arp.on = false; eng->noteOff (1, 48, *E); eng->noteOff (1, 52, *E); eng->noteOff (1, 55, *E);
    eng->panic ();

    // 9. mono legato + glide
    initPatch (*E); E->layer[0].voicing.mono = true; E->layer[0].voicing.glideTime = 0.2f;
    eng->noteOn (1, 48, 1.0f, *E); renderSeconds (*eng, *E, 0.2f, fs);
    eng->noteOn (1, 60, 1.0f, *E); s = renderSeconds (*eng, *E, 0.5f, fs);
    std::printf ("      (mono voices %d bad %ld)\n", eng->totalVoices(), s.bad);
    CHECK (eng->totalVoices() == 1 && s.bad == 0, "mono legato keeps a single voice");
    eng->panic ();

    // 10. voice stealing under heavy polyphony
    initPatch (*E); E->layer[0].voicing.voiceLimit = 4;
    for (int n = 40; n < 60; ++n) eng->noteOn (1, n, 1.0f, *E);
    s = renderSeconds (*eng, *E, 0.5f, fs);
    std::printf ("      (voices %d bad %ld)\n", eng->totalVoices(), s.bad);
    CHECK (eng->totalVoices() <= 4 && s.bad == 0, "voice limit respected with stealing");
    eng->panic ();

    // 11. CPU load: all 8 layers, 8 voices each, 3 oscs x 8 unison, 7 effects
    for (int l = 0; l < kLayers; ++l)
    {
        LayerParams& P = E->layer[l];
        P = LayerParams(); P.enabled = true;
        for (int o = 0; o < 3; ++o) { P.osc[o].on = true; P.osc[o].unison = 8; P.osc[o].table = o; }
        for (int k = 0; k < kFxSlots; ++k) P.fx[k].type = (k % 2) ? FxWeave : FxHallReverb;
        P.voicing.voiceLimit = 8;
        for (int n = 0; n < 8; ++n) eng->noteOn (1, 36 + n * 3, 1.0f, *E);
    }
    {
        std::vector<float> L (512), R (512); Transport tp;
        auto t0 = std::chrono::steady_clock::now();
        int blocks = (int) (2.0 * fs / 512.0);
        for (int b = 0; b < blocks; ++b) eng->render (L.data(), R.data(), 512, *E, tp);
        double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        double rt = ms / (2000.0 * (double) blocks * 512.0 / (2.0 * fs) / (double) blocks) ;
        std::printf ("worst case (64 voices x 24 unison + 56 fx): %.0f ms to render 2.0 s of audio -> %.1f%% of one core\n", ms, ms / 20.0);
        (void) rt;
    }


    // 12. user wavetables (Serum-style .wav files)
    {
        auto put16 = [] (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) (v & 255)); b.push_back ((uint8_t) ((v >> 8) & 255)); };
        auto put32 = [] (std::vector<uint8_t>& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) ((v >> (8 * i)) & 255)); };
        auto tag = [] (std::vector<uint8_t>& b, const char* t) { b.insert (b.end(), t, t + 4); };
        auto makeWav = [&] (const std::vector<float>& s, int bits, bool isFloat, const char* clm)
        {
            std::vector<uint8_t> b;
            tag (b, "RIFF"); put32 (b, 0); tag (b, "WAVE");
            tag (b, "fmt "); put32 (b, 16); put16 (b, isFloat ? 3 : 1); put16 (b, 1); put32 (b, 44100);
            put32 (b, (uint32_t) (44100 * bits / 8)); put16 (b, (uint32_t) (bits / 8)); put16 (b, (uint32_t) bits);
            if (clm != nullptr)
            {
                const size_t n = std::strlen (clm);
                tag (b, "clm "); put32 (b, (uint32_t) n); b.insert (b.end(), clm, clm + n); if (n & 1) b.push_back (0);
            }
            tag (b, "data"); put32 (b, (uint32_t) (s.size() * (size_t) (bits / 8)));
            for (float v : s)
            {
                if (isFloat) { uint32_t u; std::memcpy (&u, &v, 4); put32 (b, u); }
                else if (bits == 16) put16 (b, (uint32_t) (uint16_t) (int16_t) std::lround (clampf (v, -1.0f, 1.0f) * 32767.0f));
                else { int32_t q = (int32_t) std::lround (clampf (v, -1.0f, 1.0f) * 8388607.0f); b.push_back ((uint8_t) (q & 255)); b.push_back ((uint8_t) ((q >> 8) & 255)); b.push_back ((uint8_t) ((q >> 16) & 255)); }
            }
            const uint32_t riff = (uint32_t) b.size() - 8;
            for (int i = 0; i < 4; ++i) b[(size_t) (4 + i)] = (uint8_t) ((riff >> (8 * i)) & 255);
            return b;
        };

        // 8 frames of 2048 samples: frame 0 is the fundamental, frame 7 the third harmonic
        std::vector<float> stack;
        for (int f = 0; f < 8; ++f)
            for (int n = 0; n < 2048; ++n)
                stack.push_back (0.5f * std::sin (kTwoPi * (f == 7 ? 3.0f : 1.0f) * (float) n / 2048.0f));

        std::string err;
        auto wav = makeWav (stack, 32, true, "<!>2048 10000000 wavetable (test)");
        WavData wd;
        CHECK (parseWav (wav.data(), wav.size(), wd, err) && wd.samples.size() == stack.size() && wd.frameSizeHint == 2048,
               "float WAV with a 'clm ' chunk parses");
        auto tbl = buildUserTable (wd, "sine stack", err);
        CHECK (tbl != nullptr && tbl->frames == 8, "wavetable has 8 frames");
        float pk = 0.0f;
        for (int i = 0; i < 4096; ++i) pk = std::max (pk, std::fabs (tbl->read (0, 0.0f, (float) i / 4096.0f)));
        std::printf ("      (normalised peak %.3f)\n", pk);
        CHECK (std::fabs (pk - 0.9f) < 0.02f, "table is normalised to a 0.9 peak and the transform keeps the amplitude");

        auto w16 = makeWav (std::vector<float> (stack.begin(), stack.begin() + 3000), 16, false, nullptr);
        auto t16 = loadUserTableFromMemory (w16.data(), w16.size(), "x", err);
        CHECK (t16 != nullptr && t16->frames == 1, "16-bit WAV without a 'clm ' chunk uses 2048-sample frames");

        auto w24 = makeWav (std::vector<float> (stack.begin(), stack.begin() + 4096), 24, false, "<!>1024 00000000 wavetable");
        auto t24 = loadUserTableFromMemory (w24.data(), w24.size(), "x", err);
        CHECK (t24 != nullptr && t24->frames == 4, "24-bit WAV honours a 1024-sample frame size from 'clm '");

        std::vector<float> shortCycle (600);
        for (int n = 0; n < 600; ++n) shortCycle[(size_t) n] = std::sin (kTwoPi * (float) n / 600.0f);
        auto wsh = makeWav (shortCycle, 32, true, nullptr);
        auto tsh = loadUserTableFromMemory (wsh.data(), wsh.size(), "x", err);
        CHECK (tsh != nullptr && tsh->frames == 1, "a file shorter than one frame becomes a single-cycle table");

        std::vector<float> big (2048 * 300, 0.25f);
        for (int i = 0; i < (int) big.size(); ++i) big[(size_t) i] = std::sin ((float) i * 0.05f) * 0.3f;
        auto wbig = makeWav (big, 32, true, nullptr);
        auto tbig = loadUserTableFromMemory (wbig.data(), wbig.size(), "x", err);
        CHECK (tbig != nullptr && tbig->frames == UserTable::kMaxFrames, "tables with more than 256 frames are thinned to 256");

        // broken files must be rejected, never crash
        std::vector<uint8_t> garbage (200); for (size_t i = 0; i < garbage.size(); ++i) garbage[i] = (uint8_t) (i * 37 + 11);
        std::vector<uint8_t> headerOnly (wav.begin(), wav.begin() + 12);
        std::vector<uint8_t> noData (wav.begin(), wav.begin() + 36);
        std::vector<uint8_t> truncated (wav.begin(), wav.begin() + 70);
        std::vector<uint8_t> empty;
        bool rejected = loadUserTableFromMemory (garbage.data(), garbage.size(), "x", err) == nullptr
                     && loadUserTableFromMemory (headerOnly.data(), headerOnly.size(), "x", err) == nullptr
                     && loadUserTableFromMemory (noData.data(), noData.size(), "x", err) == nullptr
                     && loadUserTableFromMemory (truncated.data(), truncated.size(), "x", err) == nullptr
                     && loadUserTableFromMemory (nullptr, 0, "x", err) == nullptr;
        CHECK (rejected, "garbage, empty, header-only and truncated files are rejected");

        // play the table: pitch must be right at both ends of the morph range
        auto crossings = [&] (std::shared_ptr<UserTable> table, float morph, int note)
        {
            std::unique_ptr<Engine> en (new Engine());
            std::unique_ptr<EngineParams> ep (new EngineParams());
            en->prepare (fs, 512);
            LayerParams& P = ep->layer[0];
            P.enabled = true;
            P.osc[0].on = true; P.osc[0].unison = 1; P.osc[0].user = table.get(); P.osc[0].morph = morph;
            P.filter.type = FltOff; P.env[0].attack = 0.001f; P.env[0].sustain = 1.0f;
            en->noteOn (1, note, 1.0f, *ep);
            std::vector<float> w;
            renderSeconds (*en, *ep, 1.0f, fs, &w);
            int c = 0;
            for (size_t i = 2 * 22050 + 2; i + 2 < w.size(); i += 2) if (w[i - 2] <= 0.0f && w[i] > 0.0f) ++c;
            return c;
        };
        int c0 = crossings (tbl, 0.0f, 69), c1 = crossings (tbl, 1.0f, 69);
        std::printf ("      (A4 over 0.5 s: frame 0 -> %d cycles, frame 7 -> %d cycles)\n", c0, c1);
        CHECK (std::abs (c0 - 220) <= 3, "user table plays at the right pitch (A4 = 220 cycles in 0.5 s)");
        CHECK (std::abs (c1 - 660) <= 4, "morphing to the last frame gives the third harmonic");

        // stability over the whole keyboard with a rich table and every mip level
        bool finite = true;
        for (int note : { 0, 12, 36, 60, 84, 108, 127 })
        {
            std::unique_ptr<Engine> en (new Engine());
            std::unique_ptr<EngineParams> ep (new EngineParams());
            en->prepare (fs, 512);
            LayerParams& P = ep->layer[0];
            P.enabled = true; P.osc[0].on = true; P.osc[0].unison = 5; P.osc[0].user = tbig.get(); P.osc[0].morph = 0.37f;
            P.osc[0].fx = OfxPhaseMod; P.osc[0].fxAmt = 0.6f;
            en->noteOn (1, note, 1.0f, *ep);
            Stats st = renderSeconds (*en, *ep, 0.3f, fs);
            finite = finite && st.bad == 0;
        }
        CHECK (finite, "user table stays finite from the lowest to the highest note");

        // slot bank
        UserWaveBank bank;
        bank.assign (3, tbl); bank.assign (4, tbl);
        bool bankOk = bank.get (3) == tbl.get() && bank.getShared (4) == tbl;
        bank.clear (3);
        bankOk = bankOk && bank.get (3) == nullptr && bank.get (4) == tbl.get() && bank.getShared (4) == tbl;
        bank.assign (4, tsh);
        bankOk = bankOk && bank.get (4) == tsh.get();
        bank.collect();
        CHECK (bankOk, "wavetable slot bank assigns, shares and replaces tables");
    }


    // 13. playback positions reported to the editor (sequence and arpeggiator playheads)
    {
        std::unique_ptr<Engine> en (new Engine());
        std::unique_ptr<EngineParams> ep (new EngineParams());
        en->prepare (fs, 512);
        LayerParams& P = ep->layer[0];
        P.enabled = true; P.osc[0].on = true;
        P.seq[0].sync = false; P.seq[0].rateHz = 8.0f; P.seq[0].length = 4;
        std::vector<float> L (512), R (512);
        Transport tp;
        auto run = [&] (int blocks, std::set<int>& seqSeen, std::set<int>& arpSeen)
        {
            for (int b = 0; b < blocks; ++b)
            {
                en->render (L.data(), R.data(), 512, *ep, tp);
                tp.ppq += 512.0 * tp.bpm / 60.0 / fs;
                seqSeen.insert (en->seqPosition (0, 0));
                arpSeen.insert (en->arpPosition (0));
            }
        };
        const std::set<int> allFour = { 0, 1, 2, 3 };
        std::set<int> a, b2;
        run (2, a, b2);
        CHECK (a == std::set<int> { -1 } && b2 == std::set<int> { -1 }, "no playhead is reported while nothing is playing");

        en->noteOn (1, 60, 1.0f, *ep);
        a.clear(); b2.clear(); run (100, a, b2);
        std::printf ("      (sequence steps seen while playing:");
        for (int s : a) std::printf (" %d", s);
        std::printf (")\n");
        CHECK (a == allFour, "sequence playhead visits every step of a 4-step sequence");

        P.seq[0].retrigger = false;                                  // shared clock
        a.clear(); b2.clear(); run (100, a, b2);
        CHECK (a == allFour, "playhead also works for a sequence that does not retrigger");

        en->panic();
        a.clear(); b2.clear(); run (2, a, b2);
        CHECK (a == std::set<int> { -1 }, "playhead clears after panic");

        P.arp.on = true; P.arp.length = 4; P.arp.div = 6; P.arp.stepOn[2] = false;
        en->noteOn (1, 60, 1.0f, *ep);
        a.clear(); b2.clear(); run (100, a, b2);
        std::printf ("      (arp steps seen while playing:");
        for (int s : b2) std::printf (" %d", s);
        std::printf (")\n");
        CHECK (b2 == allFour, "arp playhead visits every step, including a switched-off one");

        en->noteOff (1, 60, *ep);
        a.clear(); b2.clear(); run (3, a, b2);
        CHECK (b2 == std::set<int> { -1 }, "arp playhead clears when the keys are released");
    }


    // 14. scales: the snapping rule, then audio checks for a sequencer routed to pitch and for the arp
    {
        auto close = [] (float a, float b) { return std::fabs (a - b) < 1.0e-4f; };
        const bool rules = close (quantizeToScale (61.0f, 1, 0), 60.0f)       // C# in C major: halfway, goes down
                        && close (quantizeToScale (63.0f, 1, 0), 62.0f)
                        && close (quantizeToScale (66.0f, 1, 0), 65.0f)
                        && close (quantizeToScale (71.0f, 1, 0), 71.0f)       // already in the scale
                        && close (quantizeToScale (63.0f, 2, 2), 62.0f)       // D# in D minor
                        && close (quantizeToScale (63.4f, 0, 0), 63.4f);      // "No scale" leaves notes alone
        CHECK (rules, "notes snap to the nearest scale note, halfway goes down, 'No scale' changes nothing");

        bool inScale = true, nearby = true, idem = true;
        for (int sc = 1; sc < kNumScales; ++sc)
            for (int key = 0; key < 12; ++key)
                for (int n = 0; n < 128; ++n)
                {
                    const float q = quantizeToScale ((float) n, sc, key);
                    const ScaleDef& d = scaleDef (sc);
                    const int pc = (((int) std::lround (q) - key) % 12 + 12) % 12;
                    bool member = false;
                    for (int i = 0; i < d.count; ++i) member = member || d.degrees[i] == pc;
                    inScale = inScale && member;
                    nearby = nearby && std::fabs (q - (float) n) <= 2.0f + 1.0e-4f;
                    idem = idem && close (quantizeToScale (q, sc, key), q);
                }
        CHECK (inScale && nearby && idem, "all 9 scales in all 12 keys: results are in the scale, within 2 semitones, and stable");

        // helper: cycles of a sine counted over the second half of a one-second render
        auto cycles = [&] (EngineParams& ep, int note)
        {
            std::unique_ptr<Engine> en (new Engine());
            en->prepare (fs, 512);
            en->noteOn (1, note, 1.0f, ep);
            std::vector<float> w;
            renderSeconds (*en, ep, 1.0f, fs, &w);
            int c = 0;
            for (size_t i = 2 * 22050 + 2; i + 2 < w.size(); i += 2) if (w[i - 2] <= 0.0f && w[i] > 0.0f) ++c;
            return c;
        };
        auto setup = [] (EngineParams& ep)
        {
            LayerParams& P = ep.layer[0];
            P.enabled = true; P.osc[0].on = true; P.osc[0].unison = 1; P.osc[0].table = 0; P.osc[0].morph = 0.0f;
            P.filter.type = FltOff; P.env[0].attack = 0.001f; P.env[0].sustain = 1.0f; P.env[0].release = 0.01f;
        };

        // sequence A raises the pitch by 3 semitones: from C that is D# (Eb)
        std::unique_ptr<EngineParams> sp (new EngineParams());
        setup (*sp);
        LayerParams& S = sp->layer[0];
        S.mod[0] = { true, SrcSeq1, TgOscPitch1, 1.0f };
        S.seq[0].sync = false; S.seq[0].rateHz = 1.0f; S.seq[0].length = 2;
        for (int i = 0; i < kMaxSteps; ++i) S.seq[0].steps[i] = 3.0f / 24.0f;
        const int none = cycles (*sp, 60);                    // 311.1 Hz -> about 156 cycles
        S.seq[0].scale = 1; S.seq[0].key = 0;                 // C major has no Eb, so it drops to D
        const int major = cycles (*sp, 60);                   // 293.7 Hz -> about 147
        S.seq[0].scale = 2;                                   // C minor has Eb
        const int minor = cycles (*sp, 60);
        std::printf ("      (sequence +3 semitones from C: no scale %d, C major %d, C minor %d cycles)\n", none, major, minor);
        CHECK (std::abs (none - 156) <= 2 && std::abs (major - 147) <= 2 && std::abs (minor - 156) <= 2,
               "a sequence routed to pitch snaps to the chosen scale");

        // the arp: a held C# in C major plays C; a +3 transpose from C lands on D
        std::unique_ptr<EngineParams> ap (new EngineParams());
        setup (*ap);
        LayerParams& A = ap->layer[0];
        A.arp.on = true; A.arp.gateLength = 1.0f;
        const int aNone = cycles (*ap, 61);                   // 277.2 Hz -> about 139
        A.arp.scale = 1; A.arp.key = 0;
        const int aMajor = cycles (*ap, 61);                  // C -> 261.6 Hz -> about 131
        for (int i = 0; i < kArpSteps; ++i) A.arp.tune[i] = 3.0f;
        const int aTuned = cycles (*ap, 60);                  // C + 3 = Eb -> D 293.7 Hz -> about 147
        std::printf ("      (arp: held C# no scale %d, C major %d; C +3 in C major %d cycles)\n", aNone, aMajor, aTuned);
        CHECK (std::abs (aNone - 139) <= 3 && std::abs (aMajor - 131) <= 3 && std::abs (aTuned - 147) <= 3,
               "the arpeggiator snaps its notes to the chosen scale");
    }

    // 15. new filter types: measured response with a sine probe
    {
        auto gainAt = [&] (int type, float cutoff, float res, float width, float hz)
        {
            StereoFilter f; f.prepare (fs);
            FilterParams fp; fp.type = type; fp.cutoff = cutoff; fp.resonance = res; fp.width = width;
            f.setup (fp, cutoffToHz (cutoff));
            double acc = 0; int n = (int) fs;
            for (int i = 0; i < n; ++i)
            {
                float l = 0.3f * std::sin (kTwoPi * hz * (float) i / fs), r = l;
                f.process (l, r);
                if (i > n / 2) acc += (double) l * l;
            }
            return std::sqrt (acc / (n / 2)) / (0.3 / std::sqrt (2.0));
        };
        const float c = 0.5f;                                   // ~ 800 Hz
        const float fc = cutoffToHz (c);
        double lo = gainAt (FltMorph, c, 0.2f, 0.0f, fc * 0.1f), hi = gainAt (FltMorph, c, 0.2f, 0.0f, fc * 8.0f);
        CHECK (lo > 0.7 && hi < 0.2, "Morph at 0 is a lowpass");
        lo = gainAt (FltMorph, c, 0.2f, 1.0f, fc * 0.1f); hi = gainAt (FltMorph, c, 0.2f, 1.0f, fc * 8.0f);
        CHECK (lo < 0.2 && hi > 0.7, "Morph at 1 is a highpass");
        double mid = gainAt (FltMorph, c, 0.6f, 0.5f, fc), off = gainAt (FltMorph, c, 0.6f, 0.5f, fc * 6.0f);
        CHECK (mid > off * 2.0, "Morph at 0.5 is a bandpass");

        // vowel: the response must change as the vowel position moves
        double a1 = gainAt (FltZVowel, 0.6f, 0.5f, 0.0f, 800.0f), a2 = gainAt (FltZVowel, 0.6f, 0.5f, 1.0f, 800.0f);
        CHECK (std::fabs (a1 - a2) > 0.05, "Z-Plane Vowel changes with the vowel position");
        double z0 = gainAt (FltZSweep, c, 0.3f, 0.0f, fc * 8.0f), z1 = gainAt (FltZSweep, c, 0.3f, 1.0f, fc * 2.0f);
        CHECK (z0 < 0.2 && z1 > 0.05, "Z-Plane Sweep: lowpass at 0, resonance moves up at 1");

        // phase filter: notches appear in the passband
        double minG = 10, maxG = 0;
        for (float hz = 100.0f; hz < 8000.0f; hz *= 1.15f) { double g2 = gainAt (FltPhase, c, 0.0f, 0.5f, hz); minG = std::min (minG, g2); maxG = std::max (maxG, g2); }
        CHECK (minG < 0.3 && maxG > 0.8, "Phase Filter has notches and a flat top");

        // comb polarity: negative feedback shifts the peaks by half a period
        float f0 = cutoffToHz (0.45f);
        double pos = gainAt (FltMorphComb, 0.45f, 0.6f, 1.0f, f0), neg = gainAt (FltMorphComb, 0.45f, 0.6f, 0.0f, f0);
        CHECK (pos > neg * 1.5, "Morph Comb: positive feedback peaks at the fundamental, negative notches it");

        // diode: lowpass character, bells: resonance at the modal ratios
        double dl = gainAt (FltDiode, c, 0.3f, 0.3f, fc * 0.1f), dh = gainAt (FltDiode, c, 0.3f, 0.3f, fc * 8.0f);
        CHECK (dl > dh * 5.0, "Acid Diode lowpasses");
        double b1 = gainAt (FltBells, c, 0.5f, 1.0f, fc), bo = gainAt (FltBells, c, 0.5f, 1.0f, fc * 1.6f);
        CHECK (b1 > bo * 3.0, "Bell Resonators ring at the fundamental, not between modes");
        CHECK (FltNumTypes == 20, "20 filter types");
    }

    // 16. effect reordering
    {
        int src[kFxSlots];
        fxMoveOrder (1, 0, src);
        CHECK (src[0] == 1 && src[1] == 0 && src[2] == 2 && src[6] == 6, "moving slot 2 to slot 1 swaps them");
        fxMoveOrder (0, 3, src);
        CHECK (src[0] == 1 && src[1] == 2 && src[2] == 3 && src[3] == 0 && src[4] == 4, "moving slot 1 to slot 4 shifts the rest up");
        fxMoveOrder (5, 5, src);
        bool same = true; for (int i = 0; i < kFxSlots; ++i) same = same && src[i] == i;
        CHECK (same, "moving a slot onto itself changes nothing");
        CHECK (fxNewPosition (0, 0, 3) == 3 && fxNewPosition (2, 0, 3) == 1 && fxNewPosition (6, 0, 3) == 6, "new positions follow the move");
        bool perm = true;
        for (int a = 0; a < kFxSlots; ++a) for (int b = 0; b < kFxSlots; ++b)
        { fxMoveOrder (a, b, src); int seen = 0; for (int i = 0; i < kFxSlots; ++i) seen |= 1 << src[i]; perm = perm && seen == (1 << kFxSlots) - 1; }
        CHECK (perm, "every move is a permutation");
    }

    // 17. sequencer gate: step switches and gate length
    {
        SeqParams sp; sp.length = 4; sp.sync = false; sp.rateHz = 4.0f; sp.smooth = 0.0f;
        for (int i = 0; i < 4; ++i) sp.steps[i] = 0.8f;
        auto run = [&] (const SeqParams& q, float& openFrac, float& step1)
        {
            StepSeq ss; ss.reset(); int open = 0, n = 0; step1 = 1.0f;
            const float dt = 1.0f / 1000.0f;                          // 4 steps/s: 250 samples per step
            for (int i = 0; i < 1000; ++i)
            {
                float v = ss.advance (dt, q, 120.0);
                if (i >= 250 && i < 500) step1 = std::min (step1, std::fabs (v) + (i == 400 ? 0.0f : 0.0f));
                open += v > 0.5f; ++n;
            }
            openFrac = (float) open / (float) n;
        };
        float frac, s1;
        run (sp, frac, s1);
        CHECK (frac > 0.99f, "default gate keeps every step open for its whole length");
        SeqParams half = sp; half.gateLength = 0.5f;
        run (half, frac, s1);
        CHECK (std::fabs (frac - 0.5f) < 0.03f, "gate length 0.5 holds each step for half its time");
        SeqParams off = sp; off.stepOn[1] = false;
        run (off, frac, s1);
        CHECK (std::fabs (frac - 0.75f) < 0.03f && s1 < 0.01f, "a step that is switched off outputs nothing");
    }

    // 18. oscillator effect helpers used by the engine and the waveform display
    {
        CHECK (std::fabs (oscFxPhase (OfxHardSync, 0.5f, 0.5f, 0.3f, 0.0f) - oscFrac (0.3f * 4.5f)) < 1e-5f, "hard sync multiplies the read phase");
        CHECK (std::fabs (oscFxPhase (OfxBend, 0.5f, 0.5f, 0.37f, 0.0f) - 0.37f) < 1e-5f, "bend at 0.5 leaves the phase alone");
        CHECK (std::fabs (oscFxPhase (OfxMirror, 0.0f, 0.5f, 0.25f, 0.0f) - 0.5f) < 1e-5f, "mirror folds the phase");
        CHECK (std::fabs (oscFxSample (OfxTube, 1.0f, 1.0f, 0.0f, oscFxTubeNorm (1.0f))) <= 1.01f, "tube output stays within the normalised range");
        CHECK (std::fabs (oscFxSample (OfxRingMod, 1.0f, 0.5f, 0.0f, 1.0f)) < 1e-6f, "ring mod at full depth follows the modulator");
        CHECK (std::fabs (oscFxSample (OfxNone, 1.0f, 0.4f, 0.9f, 1.0f) - 0.4f) < 1e-6f, "no effect leaves the sample alone");
    }

    // 19. FM mode: free routing between the three oscillators, linear and exponential
    {
        auto setup = [&] (bool fm, int type, float aFromB, float aFromA, float bFromA)
        {
            initPatch (*E);
            LayerParams& P = E->layer[0];
            P.mod[0] = { false, 0, 0, 0.0f }; P.fx[0].type = FxNone;
            P.filter.type = FltOff; P.env[0].attack = 0.0f;
            P.osc[0].unison = 1; P.osc[0].volume = 0.7f; P.osc[0].table = 0; P.osc[0].morph = 0.0f;
            P.osc[1].on = true; P.osc[1].unison = 1; P.osc[1].semi = 7; P.osc[1].volume = 0.0f; P.osc[1].table = 0; P.osc[1].morph = 0.0f;
            P.osc[2].on = false;
            P.fmMode = fm;
            P.osc[0].fmType = type; P.osc[1].fmType = type;
            P.osc[0].fmIn[1] = aFromB; P.osc[0].fmIn[0] = aFromA; P.osc[1].fmIn[0] = bFromA;
        };
        auto roughness = [&] (std::vector<float>& w)
        {
            double d = 0, e = 0;
            for (size_t i = 8; i < w.size(); ++i) { d += std::fabs (w[i] - w[i - 2]); e += std::fabs (w[i]); }
            return e > 0 ? d / e : 0.0;
        };
        auto render = [&] (Stats& st)
        {
            std::vector<float> w; eng->panic (); eng->noteOn (1, 45, 1.0f, *E);
            st = renderSeconds (*eng, *E, 0.6f, fs, &w);
            return w;
        };
        Stats st0, st1, st2, st3, st4;
        setup (false, 0, 0.0f, 0.0f, 0.0f); render (st0);                 // warm-up: settles leftovers from earlier sections
        auto w0 = render (st0);
        setup (true, 0, 0.0f, 0.0f, 0.0f);  auto w0b = render (st1);
        std::printf ("      (rms plain %.4f, fm no routing %.4f)\n", st0.rms, st1.rms);
        CHECK (st0.rms > 0.01 && std::fabs (st0.rms - st1.rms) < 0.08 * st0.rms + 1e-6, "FM mode with no routing sounds like the plain oscillator, and a silent modulator stays silent");
        setup (true, 0, 0.8f, 0.0f, 0.0f); auto wl = render (st2);
        setup (true, 1, 0.8f, 0.0f, 0.0f); auto we = render (st3);
        const double r0 = roughness (w0), rl = roughness (wl), re = roughness (we);
        std::printf ("      (roughness: plain %.3f  linear FM %.3f  exponential FM %.3f)\n", r0, rl, re);
        CHECK (st2.bad == 0 && st3.bad == 0 && rl > r0 * 1.15, "linear FM adds harmonics");
        CHECK (re > r0 * 1.05 && std::fabs (re - rl) > 0.01, "exponential FM adds harmonics and differs from linear");
        setup (true, 0, 0.5f, 0.6f, 0.4f); auto wf = render (st4);
        CHECK (st4.bad == 0 && st4.peak < 4.0 && roughness (wf) > r0, "feedback and a modulator that is itself modulated stay stable");
        // a modulator that is heard (volume up) still works, and the third oscillator can be routed in
        setup (true, 0, 0.6f, 0.0f, 0.0f); E->layer[0].osc[2].on = true; E->layer[0].osc[2].volume = 0.0f; E->layer[0].osc[2].semi = 19;
        E->layer[0].osc[1].fmIn[2] = 0.5f; E->layer[0].osc[0].fmIn[2] = 0.4f; E->layer[0].osc[2].fmIn[0] = 0.3f;
        auto w3 = render (st4);
        CHECK (st4.bad == 0 && st4.rms > 0.01, "three oscillators routed freely stay stable");
        // the oscillator effects still act inside an FM oscillator
        setup (true, 0, 0.6f, 0.0f, 0.0f); E->layer[0].osc[0].fx = OfxTube; E->layer[0].osc[0].fxAmt = 0.8f;
        auto wt = render (st4);
        CHECK (st4.bad == 0 && wt != wl, "oscillator effects still apply in FM mode");
    }

    // 20. FM amounts as modulation targets, and the shimmer reverb
    {
        auto setup = [&]
        {
            initPatch (*E);
            LayerParams& P = E->layer[0];
            P.mod[0] = { false, 0, 0, 0.0f }; P.fx[0].type = FxNone;
            P.filter.type = FltOff;
            P.osc[0].unison = 1; P.osc[0].table = 0; P.osc[0].morph = 0.0f;
            P.osc[1].on = true; P.osc[1].unison = 1; P.osc[1].semi = 7; P.osc[1].volume = 0.0f; P.osc[1].table = 0; P.osc[1].morph = 0.0f;
            P.osc[2].on = false; P.fmMode = true; P.osc[0].fmIn[1] = 0.0f;
        };
        auto roughness = [&] (std::vector<float>& w)
        {
            double d = 0, e = 0;
            for (size_t i = 8; i < w.size(); ++i) { d += std::fabs (w[i] - w[i - 2]); e += std::fabs (w[i]); }
            return e > 0 ? d / e : 0.0;
        };
        auto render = [&]
        {
            std::vector<float> w; eng->panic (); eng->noteOn (1, 45, 1.0f, *E);
            renderSeconds (*eng, *E, 0.6f, fs, &w);
            return roughness (w);
        };
        setup (); render ();
        const double plain = render ();
        setup (); E->layer[0].mod[0] = { true, SrcMacro1, TgFmBase + 1, 0.8f }; E->macros[0] = 1.0f;     // macro 1 -> "FM A from B"
        const double modded = render ();
        std::printf ("      (roughness without the FM route %.3f, with the macro raising A from B %.3f)\n", plain, modded);
        CHECK (modded > plain * 1.15, "a mod-matrix route to an FM amount opens up the FM");
        CHECK (TgFmEnd == TgFmBase + 8, "nine FM mod targets");

        CHECK (FxNumTypes == 19, "eighteen Airwindows effects");
    }

    // 21. Airwindows effects
    {
        Transport tp;
        const int N = (int) (fs * 2.0);
        std::vector<float> dry ((size_t) N, 0.0f);
        for (int i = 0; i < (int) (fs * 0.2); ++i) dry[(size_t) i] = 0.4f * std::sin (kTwoPi * 220.0f * (float) i / fs);
        for (int t = 1; t < FxNumTypes; ++t)
        {
            auto run = [&] (float mix, std::vector<float>& L)
            {
                FxSlot fx; fx.prepare (fs);
                FxParams fp; fp.type = t; fp.mix = mix;
                L = dry; std::vector<float> R = dry;
                for (int i = 0; i < N; i += 512) { int n = std::min (512, N - i); fx.process (L.data() + i, R.data() + i, n, fp, tp); }
            };
            std::vector<float> L; run (1.0f, L);
            bool finite = true; double diff = 0, tail = 0;
            for (int i = 0; i < N; ++i) { finite = finite && std::isfinite (L[(size_t) i]) && std::fabs (L[(size_t) i]) < 20.0f; diff += std::fabs (L[(size_t) i] - dry[(size_t) i]); if (i > (int) (fs * 0.5)) tail += std::fabs (L[(size_t) i]); }
            char msg[96];
            std::snprintf (msg, sizeof msg, "%s stays finite", fxTypeName (t)); CHECK (finite, msg);
            std::snprintf (msg, sizeof msg, "%s changes the signal (diff %.2f, tail %.2f)", fxTypeName (t), diff, tail); CHECK (diff > 0.01, msg);
            if (t == FxHallReverb || t == FxGuitarHall) CHECK (tail > 1.0, "reverb rings on after the input stops");
            run (0.0f, L);
            diff = 0; for (int i = 512; i < N; ++i) diff += std::fabs (L[(size_t) i] - dry[(size_t) i]);   // (the first block may still ramp from the effect's start-up mix)
            std::snprintf (msg, sizeof msg, "%s at mix 0 is dry (diff %.4f)", fxTypeName (t), diff); CHECK (diff < 1e-2, msg);
        }
    }

    // 22. learned-control modulation helpers
    {
        CHECK (std::fabs (modulatedParamValue (0.5f, 0.0f, 1.0f, 0, 0.25f) - 0.75f) < 1e-6f, "float parameter moves by a share of its range");
        CHECK (std::fabs (modulatedParamValue (-12.0f, -24.0f, 24.0f, 0, 0.5f) - 12.0f) < 1e-4f, "range is scaled to the parameter, not 0..1");
        CHECK (modulatedParamValue (0.9f, 0.0f, 1.0f, 0, 0.5f) == 1.0f, "result is clamped to the parameter range");
        CHECK (modulatedParamValue (2.0f, 0.0f, 10.0f, 1, 0.26f) == 5.0f, "integer and choice parameters step");
        CHECK (modulatedParamValue (0.0f, 0.0f, 1.0f, 2, 0.6f) == 1.0f && modulatedParamValue (1.0f, 0.0f, 1.0f, 2, -0.6f) == 0.0f, "switches flip past the halfway point");
        initPatch (*E);
        eng->panic(); eng->noteOn (1, 60, 0.5f, *E);
        std::vector<float> w; renderSeconds (*eng, *E, 0.1f, fs, &w);
        float src[SrcNum]; eng->blockSources (0, src);
        CHECK (src[SrcEnv1] > 0.0f && std::fabs (src[SrcVelocity] - 0.5f) < 1e-4f, "block-rate sources report the playing voice");
        CHECK (TgNum == TgLearned + 1, "the Learned target is last");
    }

    // 23. gate length: longer than a step, and gating the notes of the sequencer and the arpeggiator
    {
        SeqParams sp; sp.length = 4; sp.sync = false; sp.rateHz = 4.0f; sp.smooth = 0.0f;
        for (int i = 0; i < 4; ++i) sp.steps[i] = 0.8f;
        sp.stepOn[1] = false; sp.gateLength = 2.0f;
        StepSeq ss; ss.reset(); int open = 0;
        for (int i = 0; i < 1000; ++i) { float v = ss.advance (0.001f, sp, 120.0); open += ss.gateOpen && v > 0.5f; }
        CHECK (open > 970, "gate 200% carries a note through the rest after it");
        sp.gateLength = 1.5f; ss.reset(); open = 0;
        for (int i = 0; i < 1000; ++i) { ss.advance (0.001f, sp, 120.0); open += ss.gateOpen; }
        CHECK (open > 860 && open < 890, "gate 150% fills half of the rest");

        auto energy = [&] (float seqGate, bool useSeq, float arpGate, bool useArp, float release)
        {
            initPatch (*E);
            LayerParams& P = E->layer[0];
            for (auto& fxs : P.fx) fxs.type = FxNone;
            P.filter.type = FltOff;
            P.osc[0].unison = 1;
            P.env[0].release = release; P.env[0].attack = 0.002f; P.env[0].sustain = 1.0f;
            P.mod[0] = { false, 0, 0, 0.0f };
            if (useSeq)
            {
                P.seq[0].sync = false; P.seq[0].rateHz = 4.0f; P.seq[0].length = 4; P.seq[0].smooth = 0.0f; P.seq[0].gateLength = seqGate;
                P.mod[0] = { true, SrcSeq1, TgOscPitch1, 0.02f };
            }
            if (useArp)
            {
                int q = 0; for (int i = 0; i < kNumDivs; ++i) if (std::fabs (kDivCyclesPerBeat[i] - 2.0f) < 1e-3f) q = i;
                P.arp.on = true; P.arp.div = q; P.arp.gateLength = arpGate; P.arp.order = ArpUp;
            }
            std::vector<float> w; eng->panic (); eng->noteOn (1, 57, 1.0f, *E); eng->noteOn (1, 64, 1.0f, *E);
            Stats st = renderSeconds (*eng, *E, 2.0f, fs, &w);
            return st.rms;
        };
        energy (1.0f, true, 0.6f, false, 0.2f);                      // warm-up
        const double s1 = energy (1.0f, true, 0.6f, false, 0.2f), s03 = energy (0.3f, true, 0.6f, false, 0.2f);
        std::printf ("      (sequencer gate 100%% rms %.4f, 30%% rms %.4f)\n", s1, s03);
        CHECK (s03 < s1 * 0.6, "the sequencer gate shortens the notes");
        const double a1 = energy (1.0f, false, 1.0f, true, 0.6f), a03 = energy (1.0f, false, 0.3f, true, 0.6f), a2 = energy (1.0f, false, 2.0f, true, 0.6f);
        std::printf ("      (arp gate 100%% rms %.4f, 30%% rms %.4f, 200%% rms %.4f)\n", a1, a03, a2);
        CHECK (a03 < a1 * 0.7, "the arp gate shortens the notes, even with a long release");
        CHECK (a2 > a1 * 1.05, "an arp gate above 100% lets notes overlap");
    }

    // 24. audio in: an oscillator plays the host's input instead of its wavetable
    {
        initPatch (*E);
        LayerParams& P = E->layer[0];
        for (auto& fxs : P.fx) fxs.type = FxNone;
        P.filter.type = FltOff;
        P.osc[0].on = true; P.osc[0].audioIn = true; P.osc[0].volume = 1.0f; P.osc[0].unison = 8;
        P.osc[1].on = false; P.osc[2].on = false;
        const int N = 4096;
        std::vector<float> in ((size_t) N), L ((size_t) N), R ((size_t) N);
        for (int i = 0; i < N; ++i) in[(size_t) i] = 0.5f * std::sin (kTwoPi * 1000.0f * (float) i / fs);
        auto run = [&] (const float* inp, double& energyOut, double& tone)
        {
            eng->panic(); eng->noteOn (1, 48, 1.0f, *E);
            Transport tp;
            for (int i = 0; i < N; i += 512) eng->render (L.data() + i, R.data() + i, 512, *E, tp, inp ? inp + i : nullptr, inp ? inp + i : nullptr);
            double e = 0, re = 0, im = 0;
            for (int i = 2048; i < N; ++i) { e += L[(size_t) i] * L[(size_t) i]; re += L[(size_t) i] * std::cos (kTwoPi * 1000.0f * (float) i / fs); im += L[(size_t) i] * std::sin (kTwoPi * 1000.0f * (float) i / fs); }
            energyOut = std::sqrt (e / 2048.0); tone = 2.0 * std::sqrt (re * re + im * im) / 2048.0;
        };
        double e0, t0, e1, t1;
        run (nullptr, e0, t0); run (in.data(), e1, t1);
        std::printf("      (no input rms %.5f, with a 1 kHz input rms %.4f, 1 kHz amplitude %.4f)\n", e0, e1, t1);
        CHECK (e0 < 1e-6, "an Audio In oscillator is silent without input");
        CHECK (e1 > 0.01 && t1 > 0.8 * e1 * 1.414 * 0.9, "the host input is heard, unison and wavetable do not touch it");
        P.osc[0].audioIn = false; P.osc[0].table = 0;
        run (in.data(), e0, t0);
        CHECK (t0 < 0.05, "with Audio In off the input is ignored");
    }

    // 25. audio in as an FM modulator
    {
        initPatch (*E);
        LayerParams& P = E->layer[0];
        for (auto& fxs : P.fx) fxs.type = FxNone;
        P.filter.type = FltOff; P.fmMode = true;
        P.osc[0].on = true; P.osc[0].audioIn = true; P.osc[0].volume = 0.0f;       // silent itself, only modulates
        P.osc[1].on = true; P.osc[1].unison = 1; P.osc[1].volume = 1.0f; P.osc[1].table = 0; P.osc[1].fmIn[0] = 0.0f;
        P.osc[2].on = false;
        const int N = 8192;
        std::vector<float> in ((size_t) N), L ((size_t) N), R ((size_t) N);
        for (int i = 0; i < N; ++i) in[(size_t) i] = 0.8f * std::sin (kTwoPi * 300.0f * (float) i / fs);
        auto rough = [&] (float amt)
        {
            P.osc[1].fmIn[0] = amt;
            eng->panic(); eng->noteOn (1, 57, 1.0f, *E);
            Transport tp;
            for (int i = 0; i < N; i += 512) eng->render (L.data() + i, R.data() + i, 512, *E, tp, in.data() + i, in.data() + i);
            double d = 0, e = 0; for (int i = 2048; i < N; ++i) { d += std::fabs (L[(size_t) i] - L[(size_t) i - 1]); e += std::fabs (L[(size_t) i]); }
            return e > 0 ? d / e : 0.0;
        };
        rough (0.0f);
        const double plain = rough (0.0f), fm = rough (0.8f);
        std::printf("      (roughness without FM %.3f, with audio in modulating B %.3f)\n", plain, fm);
        CHECK (fm > plain * 1.2, "audio in can frequency-modulate another oscillator");
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
