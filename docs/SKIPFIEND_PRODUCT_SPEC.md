# SKIPFIEND Product Specification

Status: current implementation contract  
Product: SKIPFIEND  
Publisher: Circuit Drift Labs  
Version: 1.0.x  
Shared baseline: [Circuit Drift Labs Shared Audio Plugin Standard](standards/CDL_PLUGIN_BASELINE.md)

## 1. Product profile

SKIPFIEND is an audio effect plug-in and standalone companion that turns recent audio into controlled playback failures: skips, locked grooves, stutters, underruns, tape/dropout artifacts, corruption textures and tempo-synchronised repeat gestures.

Applicable profiles:

| Profile | Status | Notes |
|---|---|---|
| Effect plug-in | Implemented | Stereo/mono insert effect with MIDI input and optional sidechain |
| Standalone companion | Implemented | Uses JUCE standalone host wrapper |
| MIDI effect | Not applicable | MIDI controls the audio effect; MIDI is not transformed and passed through as a product |
| Offline renderer/exporter | Implemented, limited | Exports recent processed audio to WAV and retrigger events to MIDI |
| Instrument | Not applicable | Does not generate pitched instrument voices from MIDI notes |

Shipping formats for this specification: VST3, CLAP and Standalone. CLAP is produced from the same processor through clap-juce-extensions.

## 2. Supported environment and buses

The processor accepts mono or stereo main input and requires the main output layout to match. An optional mono or stereo sidechain input is accepted. Unsupported layouts are rejected.

MIDI input is enabled. MIDI output is disabled. The effect reports a 2 second tail because delay/recovery effects can ring beyond a trigger release. The core repeat engine is zero-latency and the plug-in does not report lookahead latency.

Host tempo is used when Sync is enabled and a valid host BPM exists. Otherwise Manual BPM is used. When the host is stopped or does not provide transport position, the processor advances a local phase clock. Host seeks and loop jumps re-anchor trigger timing from the latest reported PPQ position; buffered-audio playback state remains bounded by the rolling buffer.

## 3. Signal flow

1. Main input or the optional internal sample deck feeds the rolling input buffer.
2. Trigger sources determine when one or more skip engines fire while the performance gate is open.
3. Each engine configures a RepeatVoice with engine-specific failure behavior.
4. The Repeat Engine applies slice progression, pitch behavior, playback style, motion, timewarp, envelope, panning, and ending behavior.
5. Recovery artifacts and optional overlay effects are mixed with the repeated audio.
6. Wet activity and the Mix control combine wet and dry paths.
7. A safety limiter bounds the final output.
8. Post-limiter audio is written to the export history buffer and metering telemetry.

The rolling capture buffer is at least 64 seconds and may expand for Full Buffer sample-deck use, capped at five minutes.

## 4. Trigger model

The main effect is performance-gated. It runs while at least one MIDI key, the Trigger button, or Random Trigger is held/latched.

Held MIDI keys are polyphonic. White-key mapping chooses deterministic engines; black-key performance can randomise engine/feel. Multiple keys may own independent voices, subject to the fixed voice pool and voice stealing.

While the gate is open, Density, Chaos, Skip Language and Sidechain Trigger shape retrigger behavior. Trigger starts are quantised to the active rhythmic grid.

The 16-step Skip Language sequencer stores an engine ID (-1 for off) and repeat count per cell. State loading clamps engine IDs to -1..NUM_ENGINES-1 and repeats to 1..128.

## 5. Engines

| Engine | Required behavior |
|---|---|
| CD SKIP | 10–200 ms phase-oriented repeated slice with seek-recovery character |
| HARD SKIP | Larger jump/repeat with hard termination |
| STICK | Very short repeated slice with slow positional/pitch drift |
| BUFFER UNDERRUN | Playback stalls/slows rather than producing a hard digital silence |
| MP3 CORRUPT | Stylised bitcrush, pre-echo and ringing corruption |
| TAPE DROPOUT | Level sag, wow/hiss and occasional true reversed-audio recovery tail |
| RATCHET | Fast short retriggers with accelerating/timewarped, commonly ascending character |
| GATE STUTTER | Grid-derived hard gating/chopping |
| RECORD SKIP | One grid unit repeated verbatim; global pitch, timewarp, pan, envelope and dynamic Playback Style must not alter it |

RECORD SKIP invariants are enforced at voice configuration and again during live parameter updates.

## 6. Repeat Engine parameters

Stable parameter IDs are part of the preset/automation contract.

| ID | Meaning | Range / choices | Default |
|---|---|---|---|
| bypass | Internal bypass | Off/On | Off |
| bpmSync | Follow host BPM | Off/On | On |
| manualBpm | Manual tempo | 20–300 BPM | 120 |
| density | Skip density | 0–1 | 0.35 |
| chaos | Variation amount | 0–1 | 0.20 |
| mix | Dry/wet | 0–1 | 1.0 |
| grid | Rhythmic gravity | 32 bars through 1/32T | product default index |
| swing | Off-step delay | 0–1 | 0 |
| grooveLock | Resolve bursts to grid | Off/On | Off |
| artifacts | Recovery artifact level | 0–1 | 0.40 |
| repMin | Minimum repeats | 2–64 | 3 |
| repMax | Maximum repeats | 2–64 | 12 |
| sliceMin | Minimum slice | 1–500 ms | 30 ms |
| sliceMax | Maximum slice | 1–500 ms | 120 ms |
| lenMode | Slice progression | Fixed, Ramp Shorter, Ramp Longer, Random | Fixed |
| pitchMode | Pitch progression | Stable, Ascending, Descending, Chromatic, Drift | Stable |
| pitchPerSkip | Pitch step | -12..+12 semitones | 0 |
| basePitch | Constant pitch offset | -24..+24 semitones | 0 |
| timewarp | Rate progression | -1..+1 | 0 |
| volEnv | Burst envelope | Flat, Decay, Swell, Tremolo, Ducked | Flat |
| panWalk | Stereo progression | Static, Alternate, Random, Widen | Static |
| endMode | End behavior | Hard Cut, Tail Out, Glitch Click, Seek Noise, Silence/Resume | Seek Noise |
| playMode | Phrase playback | Classic, Stutter Edit, Ping-Pong, Scatter, Orbit, Evolve | Classic |
| motion | Playback-style depth | 0–1 | 0.65 |
| scTrigger | Sidechain transient triggering | Off/On | Off |
| scThresh | Sidechain threshold | -60..0 dB | -24 dB |
| seqOn | Skip Language enable | Off/On | Off |

Each engine additionally exposes stable `en_N`, `amt_N`, `prob_N` and `rate_N` parameters. Overlay rate selectors are `echoRate`, `delayRate`, `dubRate` and `revBars`.

The hidden RUIN effect is intentionally discoverable only through the easter-egg panel and uses `ruinOn`, `ruinAmt`, `ruinRate`, and `ruinTone`.

## 7. Playback Style contract

Classic preserves the straight historical repeat path.

Stutter Edit follows a repeat phrase that mixes selected reverse steps, fragment displacements, rate accents and stereo alternation.

Ping-Pong alternates repeat direction.

Scatter selects nearby fragment offsets once per repeat.

Orbit applies smooth rate breathing and stereo orbital motion.

Evolve combines controlled direction, offset, rate and stereo changes.

Motion sets the depth of the selected phrase-level choreography. Motion does not override RECORD SKIP’s verbatim invariant.

## 8. Overlay effects

Echo, Delay and Dub are tempo-derived delay overlays. Their feedback tails may continue after release.

Reverse loops the last selected number of bars backwards while held and crossfades back to normal playback on release. The available history is bounded by the rolling buffer, so oversized reverse windows clamp to captured material.

## 9. Sample deck

Supported file extensions: WAV, AIFF/AIF, FLAC, OGG, MP3 and CAF where the JUCE build has a decoder.

Loading a sample:
- must not reset or otherwise mutate the current sound design;
- starts the internal transport and enables the sample source;
- records sample name and length;
- may resize the rolling buffer when Full Buffer is enabled;
- must fail safely for missing, corrupt or unsupported media.

The sample itself is not embedded in plug-in state or user presets.

## 10. State and presets

Host state is an XML-backed JUCE ValueTree serialized with JUCE binary XML helpers.

State contains:
- the complete APVTS parameter state;
- Skip Language engine/repeat cells;
- MIDI CC mappings;
- user tooltip preference.

Transient audio buffers, loaded sample media, active voices, meters, crash-log runtime state and other ephemeral runtime data are not persisted.

Unknown/corrupt/truncated state must fail safely without crashing. Structurally valid but hostile sequencer values are clamped. Parameter IDs are stable and new parameters are appended rather than reusing existing identities.

User presets use the same state representation with the `.skipfiend` extension. Failed preset loads retain a functioning processor.

A/B stores two independent serialized snapshots.

## 11. MIDI Learn

MIDI Learn maps one CC to one parameter and one parameter to at most one learned CC.

The real-time path uses a fixed 128-entry atomic CC-to-parameter-index table. No mutex, spinlock, map mutation, string allocation or filesystem work is allowed from MIDI handling in `processBlock`.

Incoming learned CC values are posted lock-free (latest value per CC) and applied to parameters by a message-thread timer, because host parameter notification takes listener locks.

Mappings are persisted by stable parameter ID so internal table/index changes do not break saved presets.

## 12. File operations and real-time safety

The audio callback must perform no filesystem writes, state serialization or unbounded allocation.

Capture and Skip-to-MIDI are invoked from the UI/message thread.

Skip-to-MIDI audio-thread event collection uses a fixed capacity of 8,000 events. Export snapshots only the published event count.

WAV export operates outside `processBlock` and supports 16, 24 and 32-bit output.

## 13. UI and interaction

The interface follows `theme.md` and the CDL shared baseline.

Required implemented interactions:
- scalable editor;
- hover tooltips with global disable;
- right-click reset, exact value entry and MIDI learn;
- factory preset selector;
- save/open preset operations;
- A/B compare and copy;
- Trigger and Random Trigger performance controls;
- sample drag/drop and Load;
- Help, Options and Debug overlays;
- live meter, output LED, engine activity and internal telemetry feed;
- hidden RUIN easter-egg panel;
- Capture and Skip-to-MIDI actions.

The headless suite renders the editor at multiple scales and in multiple live/overlay states to catch paint/layout regressions. This complements, but does not replace, real-host interaction testing.

## 14. Diagnostics, privacy and networking

SKIPFIEND performs no analytics, telemetry upload, update checks or background network access. JUCE web-browser and curl support are disabled for the product target.

Diagnostic and crash-log files are local and opt-in. They must not contain raw audio. Users choose whether to share them.

Support contact: <https://github.com/djshellshoxxx/skipfiend/issues>  
Project homepage: <https://djshellshoxxx.github.io/circuitdriftlabs/>  
Source repository: <https://github.com/djshellshoxxx/skipfiend>

## 15. Shared-baseline compliance

| Baseline area | Status | Notes |
|---|---|---|
| Product profile | Implemented | §1 |
| Predictable controls | Implemented | APVTS attachments, visible modes, tooltips/context menus |
| Parameter contract | Implemented | §6; stable IDs; state round-trip tests |
| Accessibility/usability | Implemented with practical limits | scalable UI, non-color text/state cues; full keyboard traversal is host/JUCE dependent |
| Host/device responsibility | Implemented | plug-in does not open host devices |
| Real-time safety | Implemented | fixed voice/event/mapping structures; UI/file work outside audio callback |
| Bus/layout handling | Implemented | mono/stereo main + optional mono/stereo sidechain |
| Transport/tempo | Implemented | host/manual BPM fallback and stopped-transport phase clock |
| Bypass/levels/meters | Implemented | bypass, safety limiter, post-limiter meters |
| Host state | Implemented | APVTS + sequencer + MIDI mappings + tooltip preference |
| Presets/A-B | Implemented | factory/user presets and independent A/B snapshots |
| File loading/export | Implemented | sample deck, WAV export, MIDI event export |
| MIDI Learn | Implemented | lock-free RT mapping table |
| Help/options | Implemented | in-product manual/options/debug panels |
| Privacy/security | Implemented | no network/analytics; bounded parsers/state handling |
| Automated DSP/state QA | Implemented | headless test suite + CI |
| Automated UI rendering QA | Implemented | multiple scales/states/overlays |
| Real-host/pluginval validation | External release gate | Must be recorded when performed on a release machine/host |
| CLAP | Implemented | clap-juce-extensions wrapper; built in CI and release |

## 16. Completion definition

The repository is implementation-complete for the current VST3/CLAP/Standalone 1.0 contract when:
1. every requirement above maps to implemented code or an explicit external release gate;
2. the full headless suite passes on the final commit;
3. the VST3/CLAP/Standalone targets compile on the supported build configuration;
4. README and MANUAL contain no known-behavior contradictions;
5. no TODO/stub text describes required 1.0 behavior;
6. CI is green on the final PR head.

A host/pluginval pass is a release-validation activity, not missing product code. Its result belongs in the QA record rather than being implied by repository unit tests.
