# Troubleshooting and Validation

For full usage details, use the [in-plugin manual](../../MANUAL.md). Check the [README](../../README.md), [product specification](../SKIPFIEND_PRODUCT_SPEC.md), CI, and latest release before relying on a particular build.

## Common issues

| Symptom | Checks |
|---|---|
| No glitch or repeat is audible | Confirm audio is reaching the insert, the effect is not bypassed, Mix is audible, and a MIDI key or Trigger/Random Trigger is held or latched. The effect does not free-run. |
| Trigger is not in time | Check whether SYNC has a valid host tempo. Otherwise use TAP or set Manual BPM. Trigger starts are quantized to the selected grid. |
| Sidechain does not trigger | Enable the optional sidechain bus in the host, route a signal to it, turn on Sidechain Trigger, and set the threshold appropriately. |
| Sample deck is silent | Confirm the sample loaded, PLAY/LOOP is active, USE SAMPLE is on, and its input level is raised. Unsupported or damaged media fails safely. |
| A loaded sample disappears after reopening | Loaded sample media is intentionally not embedded in preset/project state. Load it again. |
| Playback seems behind real time after holding a loop | The playhead can accumulate lag by design. Use RESET to drop accumulated lag and rejoin real time. |
| MIDI CC is not controlling a parameter | Right-click the control, arm MIDI Learn, move the controller, and check that the host forwards MIDI to the effect. |
| User preset will not load | Confirm it is a valid `.skipfiend` preset. State loading is designed to fail safely; use a known-good preset if necessary. |

## Modeling limits

The engines create musical failure characters; they are not forensic emulations of a particular codec, tape transport, or physical player. The README and manual identify MP3 CORRUPT and TAPE DROPOUT as stylized approximations. RECORD SKIP is a deliberately constrained verbatim repeat mode.

The sample deck is a local audition aid and does not travel with presets. The reverse window cannot exceed the audio history actually buffered. The processor supports mono/stereo main input with matching output and an optional mono/stereo sidechain; unsupported layouts are rejected.

## Validation status

The repository documents a headless test harness for processor extremes, modes, parameters, presets/state, MIDI Learn, sample loading, exports, lifecycle, and UI rendering. Linux CI builds and runs this suite. Real-host/pluginval validation is documented as an external release gate and should be recorded separately.

The project README lists VST3, CLAP, and Standalone as shipping targets. Check the [Releases](https://github.com/djshellshoxxx/skipfiend/releases) page for available binaries and the [Actions](https://github.com/djshellshoxxx/skipfiend/actions) page for current build results.