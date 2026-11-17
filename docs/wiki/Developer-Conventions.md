# Developer Conventions

These are the rules the existing code follows. Match them when you change things, and check the code before you assume one.

## Code style

- C++17, JUCE idioms. Fully qualified `juce::` names, no `using namespace juce`.
- 4 space indent, braces on their own line for classes, functions and control blocks.
- `namespace skf` for DSP types in `Skipfiend.h`. `namespace P` for parameter IDs in `PluginProcessor.cpp`. `namespace col` for colour tokens in `PluginEditor.cpp`.
- Long sections in `processBlock()` are marked with banner comments like `// ---- MIDI: ... ----`. Keep the banners when you add a section, and keep them in the same order (see [[Developer-Overview]]).
- Comments explain why, not what. Many comments record a past bug or a musical reason. Keep those when you touch the code.
- Parameter IDs are `static const char*` strings. Add them to `namespace P`, not inline.
- Keep the header-only DSP in `Skipfiend.h` free of editor code.

## Real-time rules (audio thread)

The audio thread runs `processBlock()`. The rules below are how the code stays glitch free.

- No allocation in `processBlock()`. Voices are pre-sized (`voices.resize (16)`). No `new`, `std::make_unique` or `std::vector::push_back` on this path.
- No `juce::String` construction on this path. Parameter access uses `cachedParam()` with the static `const char*` IDs (see the `P::en`, `P::amt`, `P::prob`, `P::rate` helpers).
- Shared state crosses threads through `std::atomic` (`isGateOpen()`, `getRecordedEventCount()`, peaks, flash ages) or APVTS values. Do not take a `juce::ScopedLock` on the audio thread for UI state.
- `getCallbackLock()` is taken in `loadSampleFile()` while the sample buffer is swapped. Do not add new locking to `processBlock()`.
- `juce::ScopedNoDenormals` is the first line of `processBlock()`. Keep it.
- Clamp channel counts to the buffer you were given (`mainCh` in `processBlock()`). Hosts can hand you a narrower buffer than the bus layout says. Reading a missing channel crashes.
- Sidechain and other input buses must survive the block. Do not clear them.

## Gotchas

- **Engine IDs are array indexes.** `EngineId` values index the name, colour, parameter ID and sequencer tables. Adding an engine means updating all of them, plus `NUM_ENGINES`.
- **Parameter IDs are saved state.** Renaming or removing an ID breaks saved `.skipfiend` presets and host automation. Add new IDs, do not rename old ones, and keep defaults stable.
- **Plugin codes are identity.** `Fnda` (manufacturer) and `Skpf` (plugin) and the CLAP ID tell hosts which plugin this is. Changing them makes hosts treat it as a new plugin.
- **Classic is the compatibility mode.** The Playback Style "Classic" keeps the original straight repeat behaviour so old presets sound the same. Do not change Classic's behaviour when you improve the other styles.
- **Grid default is index 11 (1/16).** Changing `kDefaultGridIndex` changes every fresh instance and factory preset that relies on it.
- **Help text is duplicated.** The in-plugin Help is `kManualText` in `PluginEditor.cpp`. `MANUAL.md` is a separate copy. Update both.
- **LTO is off on purpose.** See [[Developer-Build-and-Test]].
- **Crash log is off on every load** by design. The Debug window in Help has the switch.
- **Test-only defines.** `JUCE_MODAL_LOOPS_PERMITTED=1` and `JUCE_STANDALONE_APPLICATION=1` are set only for the test target. Do not set them for the plugin.
- **Hidden RUIN effect.** A parameter group (`ruin*`) drives a deliberately undocumented effect that is off the panel. Keep it out of user docs and the manual unless the maintainer changes that.
- **Overlays append to the end of the chain.** The overlay chain runs after whatever is playing. Changing the order changes how Reverse and Echo sound.

## How to add a feature

1. Decide where it lives using [[Developer-Code-Map]].
2. If it is a parameter: add it in `makeLayout()`, add an ID in `namespace P`, read it with `cachedParam()`, and add a control in the editor. Test that it round trips through `getStateInformation()` and `setStateInformation()`.
3. If it changes audio: keep the work in the voice or processBlock path, with no allocation and no string building. Put one-time setup in `prepareToPlay()` or the constructor.
4. If it is a new engine: follow the engine checklist in [[Developer-Code-Map]].
5. Add a case to `Tests/HeadlessTests.cpp` that checks the output stays finite and in range, and run the headless harness.
6. Update `MANUAL.md`, `kManualText` and the wiki pages.
7. Open a PR. CI builds the shipping targets and runs the headless suite.

## Known limitations

From the manual and the code:

- Engines are functional models, not spectral emulations. MP3 CORRUPT and TAPE DROPOUT are lightweight approximations.
- The sample deck accepts `.wav`, `.aif`, `.aiff`, `.flac`, `.ogg`, `.mp3` and `.caf` (`isSupportedAudioFile()`).
- No automatic triggering. This is by design, not a missing feature.
- Release builds are unsigned.
- `start.bat` in the repo root is not a build script. Do not rely on it.

## Open TODOs

No `TODO`, `FIXME` or `HACK` markers in `Source/` or `Tests/`. Track open work in GitHub issues.
