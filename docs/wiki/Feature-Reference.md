# Feature Reference

This summary reflects the current README, manual, and [product specification](../SKIPFIEND_PRODUCT_SPEC.md). The specification documents stable parameter IDs and implementation contracts.

## Nine playback-failure engines

| Engine | Character |
|---|---|
| CD SKIP | Short, phase-oriented repeat with a seek/recovery feel |
| HARD SKIP | A larger playback jump and hard repeat termination |
| STICK | A tiny, drifting loop like a stuck groove |
| BUFFER UNDERRUN | A stalled or slowed burst that resumes near its tail |
| MP3 CORRUPT | Stylized bitcrush, pre-echo, and ringing artifacts |
| TAPE DROPOUT | Level sag, wow, hiss, and a reverse-recovery tail |
| RATCHET | Accelerating short retriggers, often ascending with decay |
| GATE STUTTER | Grid-synced hard gating and chopping |
| RECORD SKIP | A verbatim repeat of one grid unit, protected from other repeat shaping |

Every engine has enable, amount, and probability controls. Selection is weighted by Amount × Probability.

## Shared Repeat Engine

Every engine can use common burst controls:

- **Slice length:** fixed, ramp shorter, ramp longer, or random within a range.
- **Pitch:** stable, ascending, descending, chromatic, or drift, with per-repeat step and base offset.
- **Timewarp:** rate movement across the burst.
- **Volume envelope:** flat, decay, swell, tremolo, or ducked.
- **Pan walk:** static, alternate, random, or widen.
- **Playback Style:** Classic, Stutter Edit, Ping-Pong, Scatter, Orbit, or Evolve.
- **Motion:** controls the depth of phrase-level direction, fragment, rate, and stereo changes.
- **End behavior:** hard cut, tail out, glitch click, seek noise, or silence/resume.

Classic preserves the straight repeat behavior. RECORD SKIP keeps its verbatim invariant even when global shaping controls change.

## Timing and trigger sources

- **Rhythmic Gravity** selects a grid from long phrase lengths to fine subdivisions.
- **Swing** offsets off-steps; **Groove Lock** resolves burst duration to the grid.
- **Host Sync** uses a valid DAW tempo; Manual BPM/Tap is available without host tempo.
- **Skip Density** controls grid activity while the performance gate is open.
- **Chaos** adds controlled engine/timing/parameter variation.
- **Skip Language** is a 16-step engine/repeat sequencer.
- **MIDI performance** maps white keys to fixed engines, black keys to randomized failures, and octave to cycle rate.
- **Sidechain Trigger** detects transients on the optional sidechain bus.

The effect is performance-gated: triggers do not free-run by themselves. MIDI notes, Trigger, or Random Trigger must be held or latched.

## Additional effects and tools

- **Recovery Artifacts:** seek, laser-hunt, buffer-fill, and tape-stop-like sounds.
- **Echo, Delay, Dub:** tempo-derived overlays with feedback tails.
- **Reverse:** loops recent material backward while held and crossfades on release; the window is limited by captured history.
- **Sample deck:** audition local media without a DAW; sample media is not stored in presets.
- **Safety limiter and meters:** post-limiter level feedback and bounded peaks.
- **Presets and A/B:** factory and user presets plus two live-edit snapshots.
- **Capture:** save the current parameter state.
- **Skip-to-MIDI:** export the performed retrigger pattern.
- **WAV export:** export recent processed audio at 16/24/32-bit.
- **MIDI Learn:** map parameters to CCs through right-click context actions.
- **RUIN easter egg:** a hidden, optional texture effect.

## Product profile

SKIPFIEND is an audio effect, not a pitched instrument or a MIDI-effect that transforms and passes MIDI through. MIDI controls the audio effect. It supports mono or stereo main buses plus an optional mono/stereo sidechain. Current documented formats are VST3, CLAP, and Standalone.