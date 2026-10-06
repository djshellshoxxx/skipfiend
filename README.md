# SKIPFIEND

**Playback failure unit** — a stutter/skip VST built around one obsession: the sound of playback going wrong.

Where most glitch plugins spread across granular, spectral and corruption tools, SKIPFIEND commits fully to the stutter/repeat/skip family. It models a rolling buffer of at least the last 64 seconds of audio as a physical playback medium being abused — a bumped CD, a scratched disc, a dying drive, a corrupted MP3, a skipping DAT, a stuck cassette, a broken stream — and drives every skip from the physics of that failure.

This repo is the **SKIPFIEND CD** milestone plus a working pass at the full engine set (build steps 1–8 of the design doc): rolling buffer, all nine parallel skip engines, the full Repeat Engine underneath them, the two master controls, Rhythmic Gravity, Recovery Artifacts, a Skip Language sequencer, MIDI trigger mode, sidechain trigger, capture, and Skip-to-MIDI export.

## Build

Requirements: CMake ≥ 3.22, a C++17 compiler. JUCE 8.0.4 is fetched automatically (needs network on first configure), or point `-DSKIPFIEND_JUCE_PATH=/path/to/JUCE` at a local checkout.

### Windows (Visual Studio 2022)
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### macOS / Linux
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs (VST3 + Standalone) land in `build/SKIPFIEND_artefacts/Release/`. `COPY_PLUGIN_AFTER_BUILD` also installs the VST3 to the system plugin folder.

On memory-constrained machines, build single-threaded — LTO is already disabled in `CMakeLists.txt` for the same reason:
```
cmake --build build --config Release -- -m:1
```

### Tests

A headless harness drives the processor with no host and no audio device — every engine, every discrete mode, every parameter at both extremes, presets, randomise, A/B, state round-trips, MIDI learn, the sample deck and the export paths — asserting after every block that the output stays finite and in range.

```
cmake -S . -B build -DSKIPFIEND_BUILD_TESTS=ON
cmake --build build --target SKIPFIEND_Tests --config Release -- -m:1
./build/SKIPFIEND_Tests_artefacts/Release/SKIPFIEND_Tests
```

It prints a pass count and exits non-zero on any failure. The target is off by default and is never part of a plugin build.

It also builds the editor with no window and renders it to PNGs in your temp directory — the main panel, the help / options / debug / secret overlays, and the live states (sample loaded, gate open with engines firing, scaled to 60%, MIDI-learn armed). That makes UI regressions visible without a DAW: run the suite, then open `skipfiend_ui*.png`.

## Signal flow

```
input ─┬─────────────────────────────► dry ──────────────┐
       │                                                  │  crossfade
       └─► RollingBuffer (last ~22 s, sample-accurate) ─┐  │  by MIX × wet-activity
                                                        │  │
  trigger sources ──► Repeat voices (×8 pool) ──────────┴──┤
   • grid + Skip Density + Chaos                           │
   • Skip Language 16-step sequencer                       ├─► out
   • sidechain transient                                   │
   • MIDI notes                                            │
                                                           │
  Recovery Artifacts (seek click / laser hunt /            │
     buffer hiss / tape-stop wobble) ────────────────────► ┘
```

## The nine engines

| Engine | Failure modelled | Character |
|---|---|---|
| **CD SKIP** | laser slip | 10–200 ms chunk, pitch-stable phase-aligned loop, seek click on recovery |
| **HARD SKIP** | scratched disc losing its place | playhead jump back by 1/16, dotted-8th or a whole bar mid-phrase |
| **STICK** | stuck groove / needle in a runout | tiny slice, infinite-ish loop with slow drift |
| **BUFFER UNDERRUN** | streaming rebuffer | 40–85 % of the burst muted, resumes on the tail of the slice |
| **MP3 CORRUPT** | wrong Huffman decode | bitcrush + pre-echo + ringing phantom frequencies |
| **TAPE DROPOUT** | tape edge damage | level sag, wow on recovery, hiss |
| **RATCHET** | — | 32nd→128th retrigger burst, ascending pitch, decay envelope, accelerating |
| **GATE STUTTER** | — | hard grid-synced gate chop, clicks & cuts flavour |
| **RECORD SKIP** | needle in a locked groove | one grid unit repeated verbatim — no pitching, warping or shaping at all |

Each engine has **On / Amount / Probability**. The trigger system picks between enabled engines weighted by `Amount × Probability`.

## Repeat Engine (sits under every engine)

Per-retrigger, independent:

- **Slice length** — Fixed / Ramp Shorter (hyperstutter) / Ramp Longer / Random in range (`Slice Min/Max ms`)
- **Pitch / Repeat** — Stable / Ascending / Descending / Chromatic / Drift, with a `Pitch Step` in semitones per repeat and a global `Base Pitch` — *every skip can change pitch*
- **Timewarp** — −1 (burst decelerates to ~0.2×) … +1 (accelerates to 4×) across the burst; independent of pitch, so it reads as a time-stretch/rate ramp (the Squarepusher accelerating repeat)
- **Volume Envelope** — Flat / Decay / Swell / Tremolo / Ducked
- **Pan Walk** — Static / Alternate / Random / Widen over the burst
- **Playback Style** — Classic / Stutter Edit / Ping-Pong / Scatter / Orbit / Evolve. This is a phrase-level playback layer above the engine: it can reverse selected repeats, jump to nearby fragments, breathe the playback rate and move the burst through the stereo field instead of replaying every glitch in one direction.
- **Motion** — 0–100% depth for Playback Style. At 0 the selected mode is restrained; higher settings increase direction changes, fragment displacement, rate movement and stereo travel.
- **End Behaviour** — Hard Cut / Tail Out / Glitch Click / Seek Noise / Silence · Resume

Classic moves without preset-hunting: SOPHIE ascending burst (Pitch = Ascending, Step ≈ +2), Aphex fill (RATCHET + Decay), trip-hop 3-repeat tail (Repeats Min/Max = 3, End = Tail Out).

For a more animated edit, select **Stutter Edit** or **Evolve** and raise **Motion**. **Classic** preserves the original straight repeat behavior for compatibility with existing presets.

## Master controls

- **SKIP DENSITY** — how often the plugin fires, from ~once a bar to constant (normalised against the grid so finer grids don't runaway).
- **CHAOS** — 0 = grid-locked and deterministic; mid = probability-weighted engine swaps, drifting repeat params, occasional wrong slice; high = engines swap freely, timewarp/pitch/length re-randomise per fire.
- **Rhythmic Gravity** (`grid`) — 1/4 … 1/32T snap, with **Swing** pulling the off-steps.
- **Groove Lock** — bends the repeat count so each burst's length lands on the grid, so even chaotic skips resolve musically.

## Features

- **Skip Language sequencer** — 16 cells at the bottom of the UI. Click a cell to cycle which engine fires there (or off); mouse-wheel to set its repeat count. When **SKIP LANGUAGE** is on it drives triggering instead of the density/chaos dice.
- **MIDI Trigger Mode** — note number mod 8 selects an engine, velocity scales the repeat count. Route MIDI to the plugin and play the failure.
- **Sidechain Trigger** — enable the Sidechain bus; a transient above `SC Threshold` fires a weighted engine on the main signal (kick, hat or vocal driving the skips).
- **Recovery Artifacts** — dials in the seek click / laser hunt / buffer-fill hiss / tape-stop wobble that plays between skips, matched to the engine that just recovered.
- **CAPTURE** — writes the current full state to `~/Documents/SKIPFIEND/capture_*.skipfiend` before it drifts away.
- **SKIP→MIDI** — writes the actual retrigger pattern to `~/Documents/SKIPFIEND/skips_*.mid` (note 36 + engine index per fire) for reuse in a sampler.
- **Waveform display** — CD-transport style rolling-buffer scope with a jittering playhead, per-engine coloured skip flashes as red frame-drops, and the chaos/error log scrolling top-left.
- **MENU** — save / save as / open `.skipfiend` presets, export audio to 16/24/32-bit float WAV (last 8s, last 30s, or everything buffered), open the preset folder, Options, and About. Successful exports report the file, folder, length and quality.
- **A/B compare** — two complete snapshots. Switching slots parks the state you are leaving, so A/B always compares two live edits rather than an edit against a stale copy. `A>B` copies one over the other.
- **Options** — hover tooltips on/off, audio/MIDI device notes, and clearing every learned CC mapping.
- **Right-click any control** — MIDI learn, clear mapping, reset to default, or type an exact value.
- **Help** — the full manual in-plugin, including the GUI tour, preset locations, manual install/uninstall paths and a troubleshooting guide.
- **Debug window** (inside Help) — a live view of the raw internals, a *create log file on crash* switch (off on every load), an *export troubleshooting file* self-check, and a hard reset that clears settings, mappings and cache but never your presets or exports. Both files land in `~/Documents/SKIPFIEND/Diagnostics`.

## Layout of the code

| File | Role |
|---|---|
| `Source/Skipfiend.h` | header-only DSP: `RollingBuffer`, `RepeatParams`, `RepeatVoice` (all per-repeat modulation + MP3/tape colouring), `RecoveryArtifacts` |
| `Source/PluginProcessor.*` | parameters, transport/grid/sequencer/sidechain/MIDI triggering, engine configuration, voice pool, dry/wet crossfade, state, capture, MIDI export |
| `Source/PluginEditor.*` | the UI: `FiendLNF` look and feel implementing the house visual identity, `WaveformDisplay`, engine strip, Repeat Engine + Master panels, Skip Language cells, and the Help / Options / Debug overlays |
| `Tests/HeadlessTests.cpp` | headless test harness (off by default, see **Tests**) |
| `theme.md` / `include.md` | the shared visual identity spec and the checklist every FiendAudio plugin implements |

## Status / not yet done

The engines are functional models rather than fully spectral-accurate emulations — MP3 CORRUPT and TAPE DROPOUT in particular are lightweight approximations. Reverse-play flashes for TAPE DROPOUT are stubbed. Media presets with guest-producer fingerprints (design step 9) are not in this build.

Outstanding before release:

- **The support URLs and email are placeholders** (`fiendaudio.example`). They now live in exactly one place — `SKIPFIEND_HOMEPAGE`, `SKIPFIEND_SUPPORT_EMAIL` and `SKIPFIEND_GITHUB` at the top of `CMakeLists.txt` — and feed the VST3 module info a host displays, the About box, the in-plugin manual and the troubleshooting export. Change those three lines and everything follows.
- **No CLAP or Linux build yet** — both are planned; the CMake only produces VST3 and Standalone today.
- **GUI testing is manual.** The headless harness covers the processor thoroughly, but nothing automated exercises clicks, drags or painting. A `pluginval` run in a real host is still the last gate before shipping.


## Plain-language overview

See [ELI5: What SKIPFIEND does](ELI5.md) for a simple explanation of the effect.

## Required shared plug-in standard

This project follows the [Circuit Drift Labs Shared Audio Plugin Standard](docs/standards/CDL_PLUGIN_BASELINE.md). It is required for the plug-in target; standalone-only requirements apply only when a standalone target is included. The product-specific specification supplements the shared standard and records the applicable profiles, compliance status, and any exceptions.
