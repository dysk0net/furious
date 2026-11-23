// Stratum DSP self-test. Builds without JUCE:
//   g++ -O2 -std=c++17 -ISource/Dsp Tests/dsp_test.cpp -o dsp_test && ./dsp_test
// Renders audio for every filter / insert / oscillator-FX / effect type and checks for NaN, Inf and runaway levels.
#include "../Source/Dsp/Layer.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
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
    P.fx[0].type = FxReverb; P.fx[0].mix = 0.25f;
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
        for (int k = 0; k < kFxSlots; ++k) { E->layer[0].fx[k].type = t; E->layer[0].fx[k].mix = 0.5f; for (int st = 0; st < kMaxSteps; ++st) { E->layer[0].fx[k].gate[0][st] = (float) (st % 2 == 0); E->layer[0].fx[k].gate[1][st] = (float) (st % 3 == 0); } }
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
        P.fx[0].type = FxReverb; P.fx[1].type = FxDelay; P.fx[2].type = FxChorus; P.fx[3].type = FxPhaser;
        P.fx[4].type = FxDistortion; P.fx[5].type = FxCompressor; P.fx[6].type = FxTrancegate;
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

    std::printf ("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
