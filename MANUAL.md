# SKIPFIEND — Manual

A playback-failure stutter/skip effect. It keeps a rolling buffer of at least
the last 64 seconds
of whatever audio passes through it and abuses that buffer the way real
playback hardware fails: a bumped CD, a scratched disc, a dying drive, a
corrupted MP3, a skipping DAT, a stuck cassette.

## Quick start — testing without a DAW

SKIPFIEND is an insert effect: it needs audio flowing through it. To
audition it immediately, use the **sample deck** under the waveform display:

1. Drag a `.wav` / `.aiff` / `.flac` / `.ogg` / `.mp3` file onto the plugin
   window (the waveform display is the natural drop target, but anywhere in
   the window works), or click **LOAD**.
2. The file loads, starts looping, and **USE SAMPLE** switches on
   automatically — the effect now processes the sample instead of your
   live/host input.
3. **PLAY / LOOP** control playback; the small knob next to the deck sets
   the sample's level going into the engine.
4. Turn **USE SAMPLE** off to go back to hearing your live/host input.

The sample deck is a testing convenience — the loaded file is *not* saved
with your project or preset.

A test file, `03.wav`, is included in the project root.

## What it does

Nine parallel "engines" model different playback failures. Whichever one
fires grabs a slice of the rolling buffer and retriggers it through a
shared **Repeat Engine** that can pitch it, warp its speed, shape its
volume, walk it around the stereo field, and decide how the burst ends.

| Engine | Failure modelled | Character |
|---|---|---|
| CD SKIP | laser slip | short phase-aligned loop, seek click on recovery |
| HARD SKIP | scratched disc losing its place | playhead jumps back mid-phrase, hard cut |
| STICK | stuck groove | tiny slice loops near-forever, slow pitch drift |
| BUFFER UNDERRUN | streaming rebuffer | most of the burst muted, resumes on the tail |
| MP3 CORRUPT | wrong decode | bitcrush + pre-echo + ringing phantom tones |
| TAPE DROPOUT | tape edge damage | level sag, wow, hiss on recovery |
| RATCHET | — | accelerating retrigger burst, ascending pitch, decay |
| GATE STUTTER ("the gator") | — | hard grid-synced gate chop, clicky cut-up texture |
| RECORD SKIP | needle in a locked groove | a clean, unmangled loop of one grid unit |

Each engine has **On / Amount / Probability**; the trigger system weights
between enabled engines by Amount × Probability.

## Repeat Engine (under every engine)

Per burst: slice length (fixed / ramping shorter or longer / random), pitch
behaviour (stable / ascending / descending / chromatic / drifting) with a
pitch step and base pitch, a **Timewarp** that speeds up or slows down the
burst over its length, a volume envelope, a pan-walk pattern, and an End
Behaviour (hard cut / tail out / glitch click / seek noise / silence-resume).

## Master section

- **SKIP DENSITY** — how often new skips fire.
- **CHAOS** — 0 = grid-locked and deterministic; higher lets engines,
  timing, and repeat parameters drift and swap unpredictably.
- **MIX** — dry/wet balance.
- **RHYTHMIC GRAVITY** — the grid skips snap to, with **SWING** pulling the
  off-steps and **GROOVE LOCK** bending repeat counts to land exactly on
  the grid.
- **ARTIFACTS** — the seek-click / laser-hunt / buffer-hiss / tape-stop
  noise played between skips.
- **SIDECHAIN TRIGGER** — fires skips from transients on the Sidechain bus
  instead of the grid (enable the Sidechain input in your host).
- **MIDI MODE** — incoming MIDI notes trigger engines directly: note number
  mod 8 picks the engine, velocity sets the repeat count.
- **SKIP LANGUAGE** — the 16-step sequencer at the bottom drives triggering
  instead of the density/chaos dice. Click a cell to cycle its engine
  (including off), mouse-wheel to set its repeat count.

## How triggering works (important)

**The effect only runs while you hold something down.** There is no
free-running or automatic triggering — nothing fires on its own, ever.
There are exactly two ways to make it play:

1. **Hold a MIDI key.** Each key plays one effect for as long as it's held.
   Hold two or three keys and you get two or three effects at once.
2. **Hold the TRIGGER button.** Runs the effect at full wet while held,
   stops on release.

Everything is **quantised to the tempo in use** — a press schedules the
effect onto the next beat-grid point rather than firing wherever your
finger landed, so stacked keys and overlays all lock together.

### The playhead: looping suspends the track

SKIPFIEND plays from a **playhead** running through its buffer, not straight
from the live input — and a loop **parks that playhead**.

While you hold a loop, the track stops advancing underneath you. When you
let go, playback continues **from the end of the looped region** — the point
where you struck the loop — rather than jumping ahead to wherever the track
would have got to. Nothing in between is skipped; it's pushed later, exactly
like a needle riding a locked groove and then carrying on.

The practical consequence: after looping, output runs **behind real time** by
however long you held, and stays there. That's deliberate — it's what makes
the loop feel like part of the record rather than a filter over it. The
rolling telemetry on the right shows how far behind the playhead is, and
**RESET** drops the accumulated lag and re-joins real time.

Because the buffer holds ~64 seconds, that's the maximum lag that can build
up; past it, the playhead is pulled forward to stay inside the captured
audio.

### Full cycles

A press plays a **complete cycle** before anything else happens on that
press. The rate is how many subdivisions make up the cycle: an **8x** press
plays all eight slices, then loops the whole thing again, seamlessly. A 2x
press plays both halves; a 32x press plays all thirty-two. You always hear
the full loop of what you pressed — it never gets cut off partway by the
next trigger.

Keys still run **in parallel**: each held key owns its own cycle, so three
keys give you three complete cycles running at once, each at its own rate.
Releasing one key stops only that key's cycle; the others keep going.

The first cycle of a press starts on the grid; once a cycle has run its
course it restarts immediately, so there's no gap mid-hold.

### The keyboard map

- **White keys** each play a different effect.
- **Black keys** play a completely random effect, with random attack and
  random probability per hit — so every stab is different.
- **Octave sets the speed.** Going up the keyboard retriggers faster:
  **1x, 4x, 8x, 16x, 32x** as you climb octaves.
(RANDOM TRIGGER does the same as TRIGGER but re-randomises every engine
first.)

Everything else — Skip Density, Chaos, the Skip Language sequencer, the
Sidechain trigger — no longer *starts* anything. They shape what happens
*while* you're holding. Density sets how much of the grid gets hit while
held; at full density a hold reads as continuous stutter. With SKIP
LANGUAGE on, the 16-step sequencer decides which engine fires per step for
the duration of the hold.

Releasing always fades out fast (a few milliseconds) rather than cutting
abruptly, so holds can be stabbed rhythmically without clicking.

## Live / DJ performance

SKIPFIEND is built to be played, not just dialled in. The **performance
strip** across the top holds everything you need mid-set:

- **SYNC + BPM** — SYNC follows your host's tempo. With SYNC off (or running
  standalone with no DAW clock at all) the BPM knob sets the tempo by hand,
  so the grid stays musical when there's no host transport. The readout
  under the knob shows the tempo actually in use and whether it came from
  the HOST or is MANual.
- **DRY/WET** — the big **red** knob on the right. Fully left is clean
  signal, fully right is effect only. This is the one control to grab when
  you want the effect in or out. It's red specifically so you can find it
  without looking.
- **TRIGGER** — hold to run the effect at full wet; release to stop. This
  and a held MIDI key are the only things that make the effect play.
- **RANDOM TRIGGER** — the same, but it re-randomises every engine first,
  so each stab is a different failure. Turn on **LATCH** to make both
  trigger buttons toggle per click instead of hold-to-perform.
- **RESET** — puts every parameter back to its default. Also happens
  automatically whenever you load a new sample/track, so each track starts
  from a clean slate.
- Every control is right-click MIDI-mappable, so a controller can drive the
  dry/wet, density, chaos, or anything else hands-free.

### MIDI hold-to-perform

MIDI triggering is always live — there's no mode to switch on:

- **Holding a key** selects a factory preset (note number cycles through
  them) and plays it at **full wet** for as long as the key is held,
  re-triggering on every grid step.
- **Releasing the key** restores whatever your dry/wet was set to before.
- Different keys give genuinely different effects, so a pad controller
  becomes a bank of performance FX.

Held MIDI notes are polyphonic: each held key can own an independent effect voice/cycle, subject to the 16-voice pool and voice-stealing rules.

## Overlay FX — Echo / Delay / Dub / Reverse

Along the bottom are four **hold-to-play** effects that ride on top of
whatever the main engine is doing. Each has a rate selector next to it, and
like everything else they engage on the next quantised beat point.

- **ECHO** — tempo-synced echo, medium feedback. Rate sets the division
  (1x / 2x / 4x / 8x / 16x of a beat).
- **DELAY** — cleaner, longer repeats, less feedback.
- **DUB** — dark, high-feedback dub repeats that ring out well past
  release.
- **REVERSE** — loops the last **N bars backwards** for as long as it's
  held. The selector sets the window: **1, 2, 4, 8, 16 or 32 bars**.

Echo, Delay and Dub keep ringing out naturally after you let go rather than
cutting dead; Reverse crossfades back to the normal signal on release.

Note: the reverse window is limited by the ~64 seconds of audio the rolling
buffer holds, so a 32-bar reverse at very slow tempos will clamp to what's
actually been captured.

## Tempo and sync

Everything the plugin fires is quantised to the tempo in use, so it stays
locked to the music:

- **SYNC** follows the host's tempo, and **lights up only when the host is
  actually supplying one** — so you can tell at a glance whether you're
  really locked to the DAW or running on your own clock.
- **TAP** — tap it in time (four taps is plenty) to set the tempo by hand.
  Tapping switches SYNC off, since you're explicitly choosing the tempo.
- **BPM** knob sets the tempo directly when you're not synced. The readout
  under it shows the tempo actually in use and whether it's HOST or MAN.

## Loop / grid length

**Rhythmic Gravity** now spans **32 bars down to a 1/32 slice**: 32, 16, 8,
4, 2 and 1 bar, then 1/2, 1/4, 1/8, 1/8T, dotted 1/8, 1/16, 1/16T, 1/32 and
1/32T. Long settings give sparse, phrase-scale retriggers; short ones give
fine stutter and gate chops. It works the same on a two-second one-shot or
a full-length track.

## Bypass, randomize, presets

- **BYPASS** passes audio through untouched.
- **DICE** randomises the engines and repeat/master parameters.
- The **preset** dropdown loads a factory starting point (Init/Clean, CD
  Skip Classic, Hard Skip Chop, Vinyl Stick, Buffer Panic, Ratchet Fill,
  Trip-Hop Tail, Gate Chopper, Chaos Storm). **SAVE** / **LOAD...** store
  and recall your own full settings as `.skipfiend` files.

## MIDI Learn

Right-click **any** knob, toggle, or dropdown to map it to a MIDI CC:

- **MIDI Learn...** arms learning for that control — move a knob or fader
  on your MIDI controller and it's mapped.
- **Clear MIDI Mapping** removes an existing mapping.
- **Reset to Default** puts the control back to its default value.

A mapped control shows its CC number appended to its label (e.g. `MIX *74`).
Mappings are saved with your project/preset.

## Capture / Skip-to-MIDI

- **CAPTURE** writes the full current state to `Documents/SKIPFIEND/` for
  later reference.
- **SKIP→MIDI** writes the actual retrigger pattern just played to a `.mid`
  file (note 36 + engine index per fire) so you can reuse the rhythm in a
  sampler.

## Metering & visual feedback

The two bars on the header show post-limiter output level (green, amber
above -9 dB, red above -3 dB). A thin red flash across the top of the meter
shows the built-in safety limiter catching a peak. The waveform display's
border glows in the colour of whichever engine is currently firing,
brighter as the output gets louder, and the engine toggle buttons glow in
their own colour while actively triggering.

## Safety limiter

Because up to 8 retrigger voices can stack simultaneously, SKIPFIEND runs a
zero-latency soft-knee limiter just before output so the effect can never
slam the next plugin in your chain, regardless of how much chaos/density
you dial in.

## Resizing

The plugin window can be resized by dragging its bottom-right corner (or
via host window resizing); the whole UI scales together, aspect ratio
locked.

## Known limitations

The engines are functional models rather than fully spectral-accurate
emulations — MP3 CORRUPT and TAPE DROPOUT in particular are lightweight
approximations. TAPE DROPOUT is a stylised tape-failure model; its occasional reverse recovery now reverses the tail of the captured slice in the audio path.

## Keyboard

- **Esc** — close the in-app help window (the **? HELP** button, or press
  it again, opens/closes the same manual you're reading now).
