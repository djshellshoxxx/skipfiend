# SKIPFIEND Wiki

SKIPFIEND is a VST3 / CLAP / Standalone glitch and skip effect from Circuit Drift Labs. It fakes the failures you get from a scratched CD, a stuttering stream, a corrupt MP3 frame, a sticky groove or a dying tape deck, and it does it in time with your track.

**Who it is for:** producers and DJs who want rhythmic skips and stutters that lock to the grid, and developers who want to know how the engine is put together.

## Quick start

1. Build or install the plugin (see [[Developer-Build-and-Test]] if you are building it yourself).
2. Load SKIPFIEND on a track or bus, with a clip or a loop playing through it.
3. Hold the **TRIGGER** button, or hold a MIDI key, and let it fire.
4. Set **SKIP DENSITY** for how often it happens and **MIX** for how much of it you hear.
5. Read [[User-Guide]] for the rest.

## Pages

**For users**
- [[User-Guide]]: install, every control, workflows, FAQ
- [[Feature-Reference]]: feature list in more detail
- [[Troubleshooting-and-Validation]]: when things go wrong

**For developers**
- [[Developer-Overview]]: architecture, design decisions, signal flow
- [[Developer-Code-Map]]: where everything lives and where to edit what
- [[Developer-Build-and-Test]]: toolchain, build, tests, CI, release
- [[Developer-Conventions]]: style, real-time rules, gotchas, TODOs
- [[Architecture-and-Real-Time-Design]]: earlier notes on the real-time design
- [[Developer-Guide]]: earlier developer notes

**Also in the repo**
- README.md and MANUAL.md at the repo root (the in-plugin Help text is a separate copy embedded in `Source/PluginEditor.cpp`)
- docs/SKIPFIEND_PRODUCT_SPEC.md for the product spec
