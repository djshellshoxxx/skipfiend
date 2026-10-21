# SKIPFIEND Wiki

SKIPFIEND is a performance-gated audio effect that turns recent audio into deliberate playback failures: skips, stuck grooves, stutters, buffer underruns, tape dropouts, corruption textures, and tempo-synced repeat gestures.

The effect buffers recent input and uses a trigger to replay and reshape slices of it. It does not damage or rewrite the source audio. It can run as a VST3 or CLAP effect, or in the JUCE Standalone companion.

## Start here

- [User Guide](User-Guide.md): route audio through SKIPFIEND, trigger it, shape a sound, and save or export results.
- [Feature Reference](Feature-Reference.md): the nine failure engines, Repeat Engine, sequencer, MIDI, sidechain, overlays, and output tools.
- [Developer Guide](Developer-Guide.md): configure, build, test, and navigate the source.
- [Architecture and Real-Time Design](Architecture-and-Real-Time-Design.md): audio flow, state, and real-time constraints.
- [Troubleshooting and Validation](Troubleshooting-and-Validation.md): common setup issues, known modeling limits, and validation status.

## The core idea

SKIPFIEND treats a rolling buffer as a playback medium that can be bumped, scratched, stalled, or decoded incorrectly. Nine parallel engines model different failure characters. A shared Repeat Engine controls slice length, pitch, timewarp, envelope, stereo motion, playback style, and how each burst ends.

It is designed for hands-on performance. The main effect runs only while a performance gate is open, such as while a MIDI key or Trigger control is held or latched. Tempo/grid, density, chaos, sequencer, and sidechain settings shape what happens during that performance.

## Project status and supported targets

The repository documents VST3, CLAP, and Standalone as the current shipping formats. The README describes the implementation as feature-complete against the SKIPFIEND product specification. Automated headless tests and Linux CI are documented; real-host/pluginval validation remains an external release-validation step.

See the [README](../../README.md), [product specification](../SKIPFIEND_PRODUCT_SPEC.md), current [GitHub Releases](https://github.com/djshellshoxxx/skipfiend/releases), and [Actions](https://github.com/djshellshoxxx/skipfiend/actions) for current build and validation status.