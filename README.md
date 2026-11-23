# Stratum

An original layered hybrid synthesizer, built as a **VST3** instrument (plus a Standalone app) with
[JUCE](https://juce.com) and CMake. Eight independent synth layers can be played separately by MIDI
channel or key range, or stacked into one big patch.

Stratum is an independent project. It is not affiliated with or derived from any other synthesizer:
all code, wavetables and presets here were written from scratch.

## Features

**Per layer (x8)**
- 3 oscillators with morphable wavetables (8 built-in tables, generated at start-up with band-limited
  mip levels), up to 8 unison voices, detune/spread, bass/treble tone control, phase and phase randomisation
- Oscillator FX: tube drive, phase modulation, ring modulation, hard sync, bend, mirror, noise, chaos phase
- 13 filter types (LP 6/12/24, HP 6/12/24, band-pass, notch, peak, ladder, comb, formant) with envelope,
  keyfollow and treble controls; "skip to mix" per oscillator
- 7 insert types (tube, overdrive, tone fuzz, bitcrusher, exciter, ring mod), filter/insert order switch
- 4 envelopes (delay, attack, level, decay, sustain, release, per-stage slopes), 4 dual-waveform LFOs
  (bend, phase, fade, tempo sync), 4 step sequencers (32 steps, loop point, oneshot, smoothing)
- 8-slot modulation matrix per layer: 19 sources x 22 targets, bipolar amounts, 3 macros
- Arpeggiator: 16-step pattern with per-step on/off and transpose, 6 orders, 1-4 octaves, tempo sync
- Mono legato with glide, voice limit, MIDI channel and key range per layer
- 7-slot effect rack: reverb, delay, chorus, phaser, distortion, bitcrusher, trancegate (2 x 32 steps),
  compressor, sidechain ducker, balancer

**Master**
- 8-channel mixer (fader, pan, solo, meters), master gain, limiter, pitch bend range, key offset
- Preset browser with factory sounds and user presets (`Documents/Stratum/Presets/*.stpreset`)
- Resizable GUI (fixed 16:9, scales with the window)

Every control is a host parameter, so everything can be automated.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.8 is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Stratum_VST3 --parallel
```

The plugin bundle is written to `build/Stratum_artefacts/**/VST3/Stratum.vst3`.
Add `-DSTRATUM_COPY_AFTER_BUILD=ON` to install it straight into your system VST3 folder
(Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`,
Linux `~/.vst3`). Use `--target Stratum_Standalone` for the standalone app.

**Linux packages** (Debian/Ubuntu): `libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev
libxinerama-dev libxrandr-dev libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`

### Prebuilt binaries
Every push builds the VST3 for Windows, macOS (universal) and Linux with GitHub Actions; download the
artifact from the *Actions* tab. Tagging a commit `v0.1.0` (any `v*` tag) attaches the zips to a GitHub release.
The macOS build is unsigned, so Gatekeeper will ask you to allow it the first time.

## DSP self-test

The whole sound engine lives in `Source/Dsp` and has no JUCE dependency, so it can be tested on its own:

```bash
cmake -S . -B build -DSTRATUM_BUILD_PLUGIN=OFF
cmake --build build --target StratumDspTests && ctest --test-dir build --output-on-failure
```

It renders audio for every filter, insert, oscillator FX, wavetable and effect type, sweeps every modulation
source against every target, and checks for NaN/Inf, runaway levels, voice stealing, mono legato and the arpeggiator.

## Layout of the code

| Path | What it is |
|---|---|
| `Source/Dsp/` | Engine: wavetables, modulators, filter/insert, effects, layer + voice, master |
| `Source/Params.h` | One description of all ~2,500 host parameters (layout, engine linking, preset push) |
| `Source/FactoryPresets.h` | Built-in sounds |
| `Source/PluginProcessor.*` | MIDI, transport, state save/restore, presets |
| `Source/PluginEditor.*` | GUI |
| `Tests/dsp_test.cpp` | Engine self-test |

## Licensing

Stratum is released under the **GNU AGPL-3.0** (see `LICENSE`), which matches JUCE's open-source license.
If you want to distribute a closed-source build you need a commercial JUCE license instead.
The VST3 interface comes from the SDK bundled with JUCE; check that SDK's license terms if you redistribute binaries.
"VST" is a trademark of Steinberg Media Technologies GmbH.

## Status

Early version (0.1.0). The sound engine is covered by the self-test above. The plugin and GUI code compile against
JUCE 8.0.8, but the first full VST3 build runs on CI, so expect some rough edges to polish in the editor layout.
Ideas for next steps: sample-based oscillators and import, granular mode, a visual modulation-routing view,
more arpeggiator modes, per-layer presets.
