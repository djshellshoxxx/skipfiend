# User Guide

## What it does

SKIPFIEND sits on your audio and chops it up the way a broken playback system would. You pick a "failure" (called an engine), it fires on the beat, and it repeats a slice of whatever just played. Nothing happens on its own. The effect only runs while you hold something down, which keeps it from wrecking a mix by accident.

## Install

Grab the release for your system from the GitHub Releases page. Windows and Linux builds are published. Builds are unsigned, so Windows SmartScreen may ask you to confirm the first launch.

- **Windows standalone**: run `SKIPFIEND-<version>-Windows-x64.exe`. No install needed.
- **Windows plugin**: copy `SKIPFIEND.vst3` to `C:\Program Files\Common Files\VST3\` and `SKIPFIEND.clap` to `C:\Program Files\Common Files\CLAP\`.
- **Linux**: unpack the `.tar.gz`. Copy `SKIPFIEND.vst3` to `~/.vst3/` and `SKIPFIEND.clap` to `~/.clap/`. Run `./SKIPFIEND` for the standalone.

If a host does not see it after install, rescan plugins.

To build it yourself, see [[Developer-Build-and-Test]].

## First run

1. Put SKIPFIEND on a track with audio playing through it.
2. Hold **TRIGGER**. You should hear skips at full wet while it is held.
3. Let go. It stops.
4. Turn **SKIP DENSITY** up to get more of them, and **MIX** to blend in the dry signal.

## How triggering works

Two ways to make it play:

1. **Hold a MIDI key.** Each key plays one effect for as long as it is held. Hold two or three and you get two or three at once.
2. **Hold TRIGGER.** Full wet while held.

A press waits for the next point on the beat grid, so stacked keys and overlays lock to the tempo. White keys pick a specific engine, black keys pick a random one, and the octave sets the retrigger subdivision (1x, 4x, 8x, 16x or 32x).

## The nine engines

| Engine | What it imitates | Character |
|---|---|---|
| CD SKIP | laser slip on a CD | 10 to 200 ms chunk, phase-aligned loop, seek click on recovery |
| HARD SKIP | scratched disc losing its place | playhead jumps back by 1/16, dotted 8th or a whole bar |
| STICK | stuck groove or needle in the runout | tiny slice, near-endless loop with slow drift |
| BUFFER UNDERRUN | streaming rebuffer | 40 to 85 percent of the burst muted, resumes on the tail |
| MP3 CORRUPT | bad Huffman decode | bitcrush, pre-echo, ringing phantom frequencies |
| TAPE DROPOUT | damaged tape edge | level sag, wow on recovery, hiss |
| RATCHET | (none, it is a burst) | 32nd to 128th retrigger burst, ascending pitch, accelerating |
| GATE STUTTER | (none, it is a gate) | hard grid-synced gate chop, clicky |
| RECORD SKIP | needle in a locked groove | one grid unit repeated exactly, no pitch or shaping |

Each engine has **On**, **Amount** and **Probability**. When a trigger fires, it picks among the enabled engines weighted by Amount x Probability.

## Main controls

- **SKIP DENSITY**: how often it fires, from about once a bar to nearly constant. It is scaled to the grid so finer grids do not run away.
- **CHAOS**: 0 is fully grid-locked and predictable. Middle values swap engines by probability and drift the repeat settings. High values swap engines freely and re-randomise pitch, timewarp and length on each fire.
- **Rhythmic Gravity (grid)**: the snap size, 1/4 down to 1/32T. **Swing** pushes the off-steps later.
- **Groove Lock**: bends the repeat count so each burst ends on the grid, even when chaos is high.
- **MIX**: blend between dry and wet.
- **TRIGGER**: manual full-wet trigger, held while pressed.
- **BYPASS**: turns the effect off completely.
- **Loop / grid length**: 32 bars down to a 1/32T slice. The default is 1/16.
- **Tempo**: follows the host BPM when sync is on. Otherwise set **manualBpm** or tap tempo.

## Repeat Engine (under every engine)

Each retrigger has its own settings:

- **Slice length**: fixed, ramp shorter (hyperstutter), ramp longer, or random within Slice Min and Max (ms).
- **Pitch / Repeat**: stable, ascending, descending, chromatic or drift. **Pitch Step** is in semitones per repeat. **Base Pitch** sets the starting point.
- **Timewarp**: -1 slows the burst to about 0.2x, +1 speeds it to 4x. It moves rate, not pitch.
- **Volume Envelope**: flat, decay, swell, tremolo or ducked.
- **Pan Walk**: static, alternate, random or widen across the burst.
- **Playback Style**: classic, stutter edit, ping-pong, scatter, orbit or evolve. This moves playback around (reverses some repeats, jumps to nearby fragments) instead of replaying straight.
- **Motion** (0 to 100 percent): how strongly the playback style moves. Classic ignores it.
- **End Behaviour**: hard cut, tail out, glitch click, seek noise, silence or resume.

Some quick recipes:
- SOPHIE-style ascending burst: Pitch = Ascending, Step around +2.
- Aphex-style fill: RATCHET plus Decay.
- Trip-hop tail: Repeats Min and Max both 3, End = Tail Out.

## Skip Language sequencer

The 16 cells at the bottom of the window. Click a cell to cycle which engine fires on that step (or none). Mouse-wheel sets its repeat count. When **SKIP LANGUAGE** is on, the sequencer drives triggering instead of the density and chaos dice.

## Sidechain

Turn on the Sidechain bus. A transient above **SC Threshold** fires a weighted engine on the main signal. Good for a kick or hat driving the skips.

## Overlay FX

Four momentary overlays you can hold: Echo, Delay, Dub and Reverse. They have their own rate controls (echoRate, delayRate, dubRate, revBars). Reverse looks at a bar count (revBars).

## Recovery Artifacts

Adds the sounds that happen between skips: seek click, laser hunt, buffer-fill hiss and tape-stop wobble. Each one follows the engine that just recovered.

## Sample deck

You can load an audio file (WAV, AIFF and similar) and let SKIPFIEND play it instead of the input. You can play or loop it, set its gain, and use the full-track buffer option to keep the whole track in memory.

## MIDI Learn

Right-click any control and choose MIDI learn, then move a knob on your controller. Right-click again to clear the mapping. Options has a "clear all CC mappings" button.

## Presets, A/B and menu

- **MENU** has save, save as, open (`.skipfiend` files), export audio, open the preset folder, Options and About.
- **Factory presets** are built in. Load them from the preset list.
- **A/B**: two full snapshots. Switching slots parks the state you left, so A/B always compares two live edits. **A>B** copies one slot over the other.
- **Export audio** writes WAV at 16, 24 or 32-bit float. You can export the last 8 seconds, the last 30 seconds, or everything buffered.
- **Options**: tooltips on or off, audio and MIDI device notes, and clearing learned CC mappings.

## Capture and MIDI export

- **CAPTURE** saves the current full state to `~/Documents/SKIPFIEND/capture_*.skipfiend`.
- **SKIP to MIDI** writes the retrigger pattern to `~/Documents/SKIPFIEND/skips_*.mid`. Each fire is note 36 plus the engine index, so you can drop it into a sampler.

## Right-click menu

Right-click any control for MIDI learn, clear mapping, reset to default, or type an exact value.

## Workflows

**Live DJ use:** keep TRIGGER or a MIDI key on a pad, set Grid to 1/8, and use CHAOS low so it stays on the beat.

**Studio glitch edit:** pick one engine (say CD SKIP), set Density around the middle, put Groove Lock on, and export it from MENU. Use Capture if you want to come back to it.

**Sampler handoff:** run SKIP to MIDI on a pattern you like and load the MIDI into a sampler to rebuild it with real samples.

## Troubleshooting

See [[Troubleshooting-and-Validation]]. The short version:

- No sound while holding TRIGGER: check BYPASS is off, MIX is above zero, and the track has audio going in.
- Nothing on MIDI keys: check the host is sending MIDI to the track, and that the plugin accepts MIDI input (it does).
- Skips not on the beat: check the grid, swing, and whether host sync is on.
- Crash: enable the crash log in Help > Debug. Logs go to `~/Documents/SKIPFIEND/Diagnostics`.

## FAQ

**Does it need the host tempo?** No. Turn sync off and set a BPM by hand or by tapping.

**Why does it only work while I hold something?** By design. There is no automatic triggering, so it cannot fire unless you ask it to.

**Are the engines accurate models?** No. They are functional approximations. MP3 CORRUPT and TAPE DROPOUT in particular are lightweight imitations. The manual says so.

**Where are my presets?** `~/Documents/SKIPFIEND/` (the Help window links there).
