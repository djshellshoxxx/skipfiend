# User Guide

For the full control-by-control instructions, open [MANUAL.md](../../MANUAL.md) or press **Help** inside the plugin.

## Get audio into the effect

SKIPFIEND is an insert effect. In a DAW, put it on an audio track or bus and make sure audio is reaching that channel. It accepts matching mono or stereo main input/output layouts and has an optional mono or stereo sidechain input.

To audition without a DAW, use the sample deck below the waveform display:

1. Drag in a supported audio file or click **LOAD**.
2. Loading starts the sample transport and enables **USE SAMPLE**.
3. Use **PLAY / LOOP** and the sample-level control to audition it.
4. Turn **USE SAMPLE** off to return to the host/live input.

The sample deck is a testing convenience. Loaded sample media is not saved into a project or preset. The repository includes `03.wav` for testing.

## Trigger the effect

Nothing fires automatically. Open the performance gate by holding a MIDI key or the **TRIGGER** control. **RANDOM TRIGGER** does the same with a newly randomized engine character. If **LATCH** is enabled, the trigger controls toggle on/off per click.

- White MIDI keys select deterministic failure engines.
- Black keys select randomized failures.
- Octave selects a cycle subdivision (1x, 4x, 8x, 16x, or 32x).
- Multiple held keys can layer voices within the available voice pool.
- Releasing the final held MIDI key restores the previous dry/wet setting.

Trigger starts are quantized to the active rhythmic grid. **SYNC** follows a valid host tempo; otherwise set **BPM** manually or use **TAP**. The tempo indicator shows whether the active tempo comes from the host or manual control.

## Shape the performance

A useful first pass:

1. Choose a factory preset or start from Init/Clean.
2. Set **MIX**, **SKIP DENSITY**, and **CHAOS**. At zero chaos the behavior is more repeatable; higher values introduce variation.
3. Select **RHYTHMIC GRAVITY** and **SWING** for the timing feel. **GROOVE LOCK** can resolve burst lengths to the grid.
4. Enable one or two engine types and adjust their Amount and Probability.
5. Hold Trigger or a MIDI key while listening. Density, Chaos, the Skip Language sequencer, and Sidechain Trigger shape the activity while the gate is open.
6. Use **ARTIFACTS** for recovery sounds such as seek clicks, buffer hiss, and tape-stop wobble.
7. Adjust the Repeat Engine for slice size, pitch movement, timewarp, envelope, pan walk, playback style, and end behavior.

The 16-step **Skip Language** sequencer selects an engine (or off) and a repeat count for each cell. When enabled, it supplies the trigger pattern instead of the density/chaos trigger selection.

## MIDI and sidechain

Right-click a control for MIDI Learn, clear mapping, reset, or exact value entry. Learned mappings are saved with state. The sidechain bus can trigger skips from transients while the main input supplies the audio being processed; enable the sidechain input in the host and set its threshold.

## Save and export

- Save/open full user presets with the `.skipfiend` extension.
- **A/B** stores two snapshots; **A>B** copies one slot to the other.
- **CAPTURE** writes the current state under `Documents/SKIPFIEND`.
- **SKIP→MIDI** exports the retrigger pattern as MIDI for reuse in another instrument or sampler.
- The menu can export recent processed audio as WAV in 16-, 24-, or 32-bit formats.

The sample file itself and temporary audio/voice buffers are not embedded in preset state. See the in-product manual for exact export windows, file locations, and settings.

## Reset and bypass

**BYPASS** passes the input through. **RESET** returns parameters to defaults and rejoins real time if the buffer playhead has accumulated performance lag. Loading a sample does not reset the sound design.