# Developer Overview

## What the code is

A JUCE 8 audio plugin (C++17). One processor, one editor, one header of header-only DSP. It ships as VST3, CLAP and a Standalone app. Built with CMake, JUCE is fetched with FetchContent at 8.0.4 unless you point `SKIPFIEND_JUCE_PATH` at a local copy.

Identity strings live in one place: the top-level `CMakeLists.txt` sets homepage, support contact, GitHub URL, company (Circuit Drift Labs), manufacturer code `Fnda` and plugin code `Skpf`. CLAP id is `com.circuitdriftlabs.skipfiend`.

## Tech stack

- C++17, JUCE 8.0.4 modules: `juce_audio_utils`, `juce_audio_formats`, `juce_dsp`, plus GUI modules for the test build
- AudioProcessorValueTreeState (APVTS) for parameters, which drives host automation, state and MIDI learn
- CMake 3.22+, GitHub Actions for CI and release
- No external DSP libraries. Everything in `Source/Skipfiend.h` is hand-written.

## Design decisions (and why)

**Trigger-only, never free running.** The effect only fires while a MIDI key or TRIGGER is held. This was a deliberate choice: an automatic glitcher on a mix is hard to make musical and easy to hate. Every trigger is quantised to the beat grid so it locks to the tempo.

**A rolling buffer as the medium.** Incoming audio is pushed into a ring (`RollingBuffer`), at least 64 s (`prepareToPlay` uses `jmax(64, bufferedSeconds)`) and power-of-two sized. Repeats read from the past by absolute sample index (`roll.now()`), so a repeat can point at audio that already played. That is what makes "skip back into what you just heard" possible without a separate capture step.

**Voice pool instead of allocation.** Each repeat is a `RepeatVoice` in `std::vector<RepeatVoice> voices`. `allocVoiceIndex()` finds a free one. The pool is sized once to 16 at construction (`voices.resize (16)`), so the audio thread does not allocate per fire.

**Engines are configurations, not separate DSP graphs.** The nine engines (`skf::EngineId`) share one repeat voice. `configureEngine()` sets the parameters for each engine (slice length, pitch behaviour, colouring) and `fireEngine()` starts a voice from it. This keeps the code small and makes the Repeat Engine controls work under every engine.

**Parameters are read from a cache on the audio thread.** `cachedParam()` reads cached floats keyed by fixed `const char*` IDs (see the `P` namespace in `PluginProcessor.cpp`). Parameter IDs are static tables so the real-time path never builds a `juce::String`.

**Presets and state use the APVTS.** `getStateInformation` and `setStateInformation` save the tree, so a `.skipfiend` preset is the same as a DAW state chunk. A/B slots, the sample file path and MIDI mappings are stored alongside.

**Honest tradeoffs.** The engines are functional models, not spectral emulations. MP3 CORRUPT and TAPE DROPOUT are approximations. LTO is switched off in CMake because the link step is very memory hungry on the build machines this project usually runs on. Turn it back on in `CMakeLists.txt` if you have the RAM.

## Signal flow

```
 host audio in ──┬──────────────────────────────► dry ──────────────┐
                 │                                                  │
                 ├─► roll (RollingBuffer, >= 64 s)                  │  crossfade
                 │     ▲                                            │  by MIX x
                 │     │ absolute sample index                      │  wet activity
                 │  ┌──┴────────────────────────────┐               │
 triggers ───────┼─►│ voices[16]  (RepeatVoice)     │──► wet ───────┤
  - MIDI keys    │  │  fireEngine() / configureEngine│              │
  - TRIGGER btn  │  └───────────────────────────────┘               ├──► out
  - grid + density / chaos                                         │
  - Skip Language sequencer                                        │
  - sidechain transient (sc bus) ─────────────────────────────────┤
                 │                                                  │
 Recovery Artifacts (click, hunt, hiss, wobble) ─────────────────► ┘
 Overlay FX (echo, delay, dub, reverse) uses roll / fxRoll
```

`processBlock()` (`Source/PluginProcessor.cpp`, roughly line 1086) runs in this order each block, as marked by its section comments:

1. transport (host BPM or manual, grid, tempo)
2. test-sample deck (replaces the input when armed)
3. feed the rolling buffer with dry input
4. MIDI: hold-to-perform keys and always-on CC to parameter map
5. the gate: nothing fires unless a key or a trigger button is held
6. held MIDI keys, TRIGGER / RANDOM TRIGGER, Skip Language sequencer and density skips (each one a self-looping cycle)
7. sidechain transient re-triggering (also gated)
8. overlay effects: armed on press, engaged on the next quantise point
9. render voices and recovery artifacts, crossfade dry/wet, overlays, safety limiter
10. post: fire recovery artifacts for voices that just ended, telemetry

## Threads

- **Audio thread**: `processBlock()` only. Reads atomics and the parameter cache. Does not take the UI locks.
- **Message thread**: editor, presets, file dialogs, export triggers, MIDI learn UI. Reads atomics like `isGateOpen()`, `getRecordedEventCount()` and `getLiveDebugDump()` for display.
- **Locks**: `getCallbackLock()` is held while the sample file is swapped in `loadSampleFile()`. Smaller `ScopedLock`s protect `sampleName` and `tapLock`. The audio thread never waits on UI state.
- **Timers**: the waveform display and the editor's meters refresh from a `juce::Timer`.

## Data

- `skf::RollingBuffer`: stereo float ring, power-of-two length, absolute index, read and write helpers.
- `skf::RepeatParams`: everything one repeat needs (slice length, pitch mode, timewarp, volume env, pan walk, playback style, motion, end behaviour).
- `skf::RepeatVoice`: one running repeat, owns its position and envelopes, renders into a buffer.
- `skf::RecoveryArtifacts`: the seek click, laser hunt, hiss and wobble generators.
- `skf::TempoDelay`, `skf::ReverseLooper`, `skf::SafetyLimiter`: helpers used by overlays and the output stage.

Persistence is the APVTS state (XML/binary via JUCE). Diagnostics and captures live in `~/Documents/SKIPFIEND/`.

## Where the UI fits

`PluginEditor` builds the layout. `FiendLNF` (a `LookAndFeel_V4`) gives the house look from `theme.md`. Controls are small wrappers (`Knob`, `Combo`, `Toggle`, `TextBtn`, `MomentaryBtn`) bound to APVTS parameters. Overlays (Help, Options, Debug, the secret panel) are child components shown on top. The editor never writes DSP state directly; it sets APVTS values and reads atomics.

See [[Developer-Code-Map]] for file and function locations, and [[Developer-Conventions]] for the rules.
