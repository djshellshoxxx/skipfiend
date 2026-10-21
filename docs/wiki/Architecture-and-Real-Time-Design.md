# Architecture and Real-Time Design

See the [product specification](../SKIPFIEND_PRODUCT_SPEC.md) for detailed contracts. This page gives contributors a map of the audio path and its thread responsibilities.

## Audio path

1. Main input or the optional internal sample deck feeds the rolling input buffer.
2. MIDI, Trigger/Random Trigger, grid/sequence, and sidechain transient logic select when engines fire while the performance gate is open.
3. Each active engine configures a RepeatVoice from the shared voice pool.
4. The Repeat Engine reads buffered slices and applies length, pitch, timewarp, envelope, pan, playback style, and ending behavior.
5. Recovery artifacts and overlay effects are mixed with the repeated sound.
6. Wet activity and Mix combine the dry and wet paths.
7. A safety limiter bounds output; post-limiter audio and meters feed the export history and UI telemetry.

The rolling buffer retains at least 64 seconds. The sample-deck Full Buffer option may expand it up to five minutes. Reverse and other history-dependent features are bounded by available buffered material.

## Source responsibilities

| File | Responsibility |
|---|---|
| `Source/Skipfiend.h` | DSP structures: `RollingBuffer`, `RepeatParams`, `RepeatVoice`, recovery-artifact processing |
| `Source/PluginProcessor.*` | Parameters, audio/MIDI/sidechain processing, timing, state, voice pool, capture/export |
| `Source/PluginEditor.*` | User interface and display components |
| `Tests/HeadlessTests.cpp` | Processor and editor test harness |

## Real-time constraints

The audio callback must avoid filesystem operations, serialization, unbounded allocations, and blocking locks. MIDI CC mapping uses a fixed atomic lookup path; parameter notifications are applied on the message thread. Capture and file exports run outside `processBlock`. Skip-to-MIDI collection uses a fixed-capacity event buffer, and export snapshots the published events off the audio thread.

The processor does not open a DAW host’s audio device. The host owns device routing; the Standalone wrapper handles its own audio device configuration.

## State and saved work

Host state persists APVTS parameters, Skip Language cells, MIDI CC mappings, and tooltip preference. User presets use the `.skipfiend` extension, and A/B keeps two independent snapshots. The loaded sample, rolling buffer, active voices, meters, and other transient runtime data are not saved. A failed preset load is intended to leave the processor usable.

Capture and WAV/MIDI exports are local files. The product specification states the plugin has no analytics, telemetry upload, update checks, or background network access.