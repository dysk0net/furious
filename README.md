# Furious

An original layered hybrid synthesizer, built as a **VST3** instrument (plus a Standalone app) with
[JUCE](https://juce.com) and CMake. Eight independent synth layers can be played separately by MIDI
channel or key range, or stacked into one big patch.

Furious is an independent project. It is not affiliated with or derived from any other synthesizer:
all code, wavetables and presets here were written from scratch.

## Features

**Per layer (x8)**
- 3 oscillators with morphable wavetables (8 built-in tables, generated at start-up with band-limited
  mip levels), up to 8 unison voices, detune/spread, bass/treble tone control, phase and phase randomisation
- FM mode (switch on any layer; layer 8 starts in it): the three oscillators modulate each other with free
  routing (every oscillator has In A / In B / In C amounts, so loops and self-feedback work), linear or exponential
  FM per oscillator; every FM amount is also a modulation-matrix target; a silent-volume oscillator works as a pure modulator
- Oscillator FX: tube drive, phase modulation, ring modulation, hard sync, bend, mirror, noise, chaos phase; the oscillator display shows the effect on the waveform (dry waveform drawn faintly behind it)
- 20 filter types: LP/HP 6/12/24, band-pass, notch, peak, ladder, comb, formant, plus Z-Plane Vowel (three
  morphing formants), Z-Plane Sweep (moving pole pair), Morph LP-BP-HP, Morph Comb (signed feedback), Phase Filter
  (all-pass chain), Acid Diode (nonlinear ladder) and Bell Resonators (harmonic to inharmonic modes); the Width
  knob is relabelled per type. Envelope, keyfollow and treble controls; "skip to mix" per oscillator
- 7 insert types (tube, overdrive, tone fuzz, bitcrusher, exciter, ring mod), filter/insert order switch
- 4 envelopes (delay, attack, level, decay, sustain, release, per-stage slopes), 4 dual-waveform LFOs
  (bend, phase, fade, tempo sync), 4 step sequencers (32 steps, per-step on/off, gate length, loop point, oneshot, smoothing, scale lock)
- 8-slot modulation matrix per layer: 19 sources x 31 targets, bipolar amounts, 3 macros
- Arpeggiator: 16-step pattern with per-step on/off and transpose, 6 orders, 1-4 octaves, tempo sync
- Scale lock for the arpeggiator and for each sequencer (when it drives an oscillator's pitch): Major, Minor,
  Phrygian, Blues, Phrygian Dominant, Double Harmonic, Hungarian Minor, Hirajoshi and Whole Tone, in any key.
  Notes snap to the nearest scale note; a note exactly halfway between two goes to the lower one
- Mono legato with glide, voice limit, MIDI channel and key range per layer
- 7-slot effect rack incl. a shimmer reverb (drag a slot up or down in the list to reorder; its settings, trancegate pattern and
  mod routes move with it): reverb, delay, chorus, phaser, distortion, bitcrusher, trancegate (2 x 32 steps),
  compressor, sidechain ducker, balancer

**Master**
- 8-channel mixer (fader, pan, solo, meters), master gain, limiter, pitch bend range, key offset
- Preset browser with factory sounds and user presets (`Documents/Furious/Presets/*.stpreset`)
- Resizable GUI (fixed 16:9, scales with the window)

Every control is a host parameter, so everything can be automated.

## Your own wavetables

Furious loads wavetables in the same `.wav` format Serum uses. Put them in your Documents folder, in a folder you
create yourself (it is not created automatically):

- Windows: `C:\Users\<you>\Documents\Furious\Wavetables\`
- macOS / Linux: `~/Documents/Furious/Wavetables/`

(If you made a `Documents/Stratum` folder under the plug-in's earlier name and have no `Documents/Furious`, it is still used.)

Click an oscillator's waveform display and choose a file under **User wavetables**. Subfolders become submenus,
and the list is rescanned every time the menu opens, so new files show up without restarting. The **Morph**
control scrolls through the frames of the table.

| | |
|---|---|
| Files | `.wav`, 8/16/24/32-bit PCM or 32/64-bit float, mono (stereo files use the left channel) |
| Frames | single cycles stacked one after another, 2048 samples each; an optional `clm ` chunk (`<!>1024 ...`) sets another frame length |
| Limits | 256 frames (longer files are thinned evenly); a file shorter than one frame is used as a single cycle |
| Processing | normalised to a common peak, DC removed, and rebuilt as band-limited copies so high notes do not alias |

The chosen file is stored in presets and in the host project as a path relative to the wavetable folder, so a
preset moved to another computer needs the same file in the same place. If it is missing, the display shows
**MISSING** in red and the oscillator falls back to its factory table.

## Building

Requires CMake 3.22+ and a C++17 compiler. JUCE 8.0.8 is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Furious_VST3 --parallel
```

The plugin bundle is written to `build/Furious_artefacts/**/VST3/Furious.vst3`.
Add `-DFURIOUS_COPY_AFTER_BUILD=ON` to install it straight into your system VST3 folder
(Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`,
Linux `~/.vst3`). Use `--target Furious_Standalone` for the standalone app.

**Linux packages** (Debian/Ubuntu): `libasound2-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev
libxinerama-dev libxrandr-dev libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev`

### Prebuilt binaries
Every push builds the VST3 for Windows, macOS (universal) and Linux with GitHub Actions; download the
artifact from the *Actions* tab. Tagging a commit `v0.1.0` (any `v*` tag) attaches the zips to a GitHub release.
The macOS build is unsigned, so Gatekeeper will ask you to allow it the first time.

## DSP self-test

The whole sound engine lives in `Source/Dsp` and has no JUCE dependency, so it can be tested on its own:

```bash
cmake -S . -B build -DFURIOUS_BUILD_PLUGIN=OFF
cmake --build build --target FuriousDspTests && ctest --test-dir build --output-on-failure
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

Furious is released under the **GNU AGPL-3.0** (see `LICENSE`), which matches JUCE's open-source license.
If you want to distribute a closed-source build you need a commercial JUCE license instead.
The VST3 interface comes from the SDK bundled with JUCE; check that SDK's license terms if you redistribute binaries.
"VST" is a trademark of Steinberg Media Technologies GmbH.

## Status

Early version (0.1.0). The sound engine is covered by the self-test above. The plugin and GUI code compile against
JUCE 8.0.8, but the first full VST3 build runs on CI, so expect some rough edges to polish in the editor layout.
Ideas for next steps: sample-based oscillators and import, granular mode, a visual modulation-routing view,
more arpeggiator modes, per-layer presets.
