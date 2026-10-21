# Developer Code Map

Start here if you are an LLM or a new contributor and need to find something fast. Paths are relative to the repo root. Line numbers are approximate and drift as the code changes, so search for the symbol name if a line number is off.

## Directory tree

```
skipfiend/
├── CMakeLists.txt              build definition: JUCE fetch, plugin target, CLAP, tests, brand strings
├── include.md                  house checklist for every VST (help, icon, presets, reset, export...)
├── theme.md                    Circuit Drift Labs palette and visual identity spec
├── README.md / MANUAL.md       user-facing docs (the in-plugin Help is a separate embedded copy, see below)
├── ELI5.md                     plain-language overview
├── start.bat                   not a build or launch script: a plain text note (ignore it for build purposes)
├── Source/
│   ├── Skipfiend.h             namespace skf: header-only DSP (engines, repeat voice, rolling buffer, recovery, limiter)
│   ├── PluginProcessor.h/.cpp  audio processor: parameters (APVTS), triggers, voices, overlays, state, export, MIDI
│   ├── PluginEditor.h/.cpp     UI: look and feel, controls, waveform, overlays, menus, timers
│   ├── icon.png, icon_small.png  plugin icons (CMake references these)
├── Tests/
│   └── HeadlessTests.cpp       headless harness (processor-only, no host, renders UI to PNG)
├── shared/scripts/             house Python and shell tools (version bump, release notes, Sparkle/WinSparkle, port audit, project inspection, VM registry). Not referenced by CI or CMake
├── docs/
│   ├── SKIPFIEND_PRODUCT_SPEC.md   product spec
│   ├── standards/CDL_PLUGIN_BASELINE.md   Circuit Drift Labs plugin baseline
│   └── wiki/                   these wiki pages (synced to the GitHub wiki by CI, see below)
├── skills/                     bundled agent skills and third-party licence notices (not plugin code)
├── .github/
│   ├── workflows/ci.yml        build + headless tests on push to main / audit/** and PRs to main
│   ├── workflows/release.yml   tagged or version-bump release: Windows-x64 and Linux-x64 packages
│   └── agents/feature-auditor.agent.md   agent prompt for feature audits
└── .gitignore
```

## Key symbols

### Source/Skipfiend.h (namespace `skf`)

| Symbol | Purpose |
|---|---|
| `enum EngineId` | CDSKIP, HARDSKIP, STICK, BUFFERUR, MP3CRPT, TAPEDO, RATCHET, GATESTUT, RECSKIP, NUM_ENGINES (9) |
| `engineName()`, `engineColour()` | display names and per-engine colours |
| `gridNames()`, `gridBeats()`, `kNumGrids = 15`, `kDefaultGridIndex = 11` | grid table (32 bars to 1/32T); default 1/16 |
| `struct RollingBuffer` | stereo ring buffer, power-of-two length, `prepare(sr, ch, seconds)`, absolute `now()` indexing |
| `struct RepeatParams` | all per-repeat settings (slice, pitch, timewarp, volume env, pan, playback style, motion, end) |
| `struct RepeatVoice` | one running repeat; renders into a buffer; owns its envelopes and colouring (MP3/tape) |
| `struct RecoveryArtifacts` | seek click, laser hunt, buffer hiss, tape-stop wobble |
| `struct TempoDelay` | tempo-synced delay used by overlays |
| `struct ReverseLooper` | reverse overlay |
| `struct SafetyLimiter` | final output limiter |

### Source/PluginProcessor.h/.cpp

| Symbol (file:approx line) | Purpose |
|---|---|
| `namespace P` (cpp:5) | every parameter ID as a `const char*`, plus fixed tables for per-engine IDs (`en_N`, `amt_N`, `prob_N`, `rate_N`) |
| `makeLayout()` (cpp:91) | builds the APVTS parameter layout; add new params here |
| `cachedParam()` (cpp:173) | audio-thread-safe parameter read from the cache |
| `prepareToPlay()` (cpp:182) | sets up rolling buffers (min 64 s), sample rate dependent state |
| `pickEngineWeighted()`, `randomEnabledEngine()` (cpp:232, 248) | engine selection for density and chaos |
| `applyLiveParamsToVoice()` (cpp:263) | pushes live knob values into running voices |
| `allocVoiceIndex()` (cpp:297) | finds a free voice in the pool of 16 |
| `configureEngine()` (cpp:324) | maps an engine ID to its `RepeatParams` |
| `fireEngine()` (cpp:436) | starts a voice for an engine at an anchor point |
| `handleMidiCC()`, `flushPendingMidiCC()`, `startMidiLearn()`, `clearMidiMapping()` (cpp:615 to 671) | CC to parameter mapping and MIDI learn |
| `loadSampleFile()`, `setSamplePlaying()`, `setSampleLooping()` (cpp:697 to 761) | test-sample deck; swaps the buffer under `getCallbackLock()` |
| `randomizeSkipParams()`, `randomizeAll()` (cpp:773, 984) | randomise |
| `resetAllToDefaults()` (cpp:817) | reset |
| `tapTempo()`, `resetTapTempo()` (cpp:845, 851) | tap tempo |
| `setManualTrigger()`, `engageRandomTrigger()` (cpp:880, 885) | TRIGGER button and random trigger |
| `getFactoryPresetNames()`, `loadFactoryPreset()` (cpp:897, 903) | built-in presets |
| `storeToSlot()`, `recallSlot()`, `copyCurrentSlotToOther()` (cpp:994 to 1017) | A/B slots |
| `exportAudioToWav()` (cpp:1033) | WAV export, 16/24/32-bit float |
| `savePresetToFile()`, `loadPresetFromFile()` (cpp:1071, 1078) | `.skipfiend` files |
| `processBlock()` (cpp:1086) | the whole audio callback |
| `getStateInformation()`, `setStateInformation()` (cpp:1787, 1821) | state save and restore |
| `doCapture()`, `doMidiExport()` (cpp:1879, 1888) | CAPTURE and SKIP to MIDI |
| `getDiagnosticsDir()`, `buildTroubleshootingReport()`, `getLiveDebugDump()` (cpp:1918 to 2049) | diagnostics |
| `setCrashLogEnabled()`, `hardResetAndClearCache()` (cpp:2113, 2154) | crash log and hard reset |
| `createPluginFilter()` (cpp:2186) | JUCE plugin entry point |
| `SkipfiendAudioProcessor` members `roll`, `chainRoll`, `fxRoll`, `voices` (h:231 to 234) | buffers and voice pool |
| hidden RUIN effect (`ruinOn`, `ruinAmt`, `ruinRate`, `ruinTone`) | intentionally not on the panel and not documented in the user guide |

### Source/PluginEditor.h/.cpp

| Symbol | Purpose |
|---|---|
| `class FiendLNF` (h:12, cpp:493 onward) | look and feel: rotary and linear sliders, buttons, combo, tooltips |
| `namespace col` (cpp:3) | colour tokens from theme.md |
| `class WaveformDisplay` (h:39) | rolling scope with playhead, skip flashes and log (`timerCallback` cpp:700, `paint` cpp:830) |
| `Knob`, `Combo`, `Toggle`, `TextBtn`, `MomentaryBtn`, `CanvasComp`, `LearnHighlight`, `LevelMeter`, `SeqCell` (h:110 to 270) | control wrappers and custom components |
| `HelpOverlay`, `DebugOverlay`, `OptionsOverlay` (h:193 to 223) | overlay panels |
| `addKnob()`, `addCombo()`, `registerParamControl()` (cpp:1509 to 1554) | bind controls to APVTS IDs |
| `showMainMenu()` (cpp:2301) | MENU: save, open, export, options, about |
| `exportAudioViaChooser()` (cpp:2371) | export UI |
| `showParamMenu()` (cpp:2498) | right-click menu: MIDI learn, reset, type value |
| `toggleSecretPanel()` (cpp:2440) | opens the hidden panel |
| `timerCallback()` (cpp:2559) | UI refresh from atomics |
| `paintCanvasContents()`, `layoutCanvasContents()` (cpp:2724, 2814) | main canvas paint and layout |
| `keyPressed()` (cpp:2197) | keyboard shortcuts |
| `filesDropped()`, `tryLoadSample()` (cpp:2222, 2232) | drag and drop a sample |

### Tests/HeadlessTests.cpp

Processor-only tests. Drives every engine, every mode, extremes of each parameter, presets, A/B, state round trips, MIDI learn, sample deck and export, and checks output is finite and in range after each block. Also builds the editor with no window and writes `skipfiend_ui*.png` to the temp directory.

## Where do I find X / to change X, edit Y

| I want to... | Edit |
|---|---|
| add a parameter | `makeLayout()` in `PluginProcessor.cpp`, add an ID in `namespace P`, read it with `cachedParam()`, add a control in `PluginEditor.cpp` (`addKnob` or `addCombo`) |
| add an engine | `EngineId` and the name and colour tables in `Skipfiend.h`, `NUM_ENGINES`, the `enIds`/`amtIds`/`probIds`/`rateIds` tables in `PluginProcessor.cpp`, `configureEngine()`, `fireEngine()`, and the engine strip in `PluginEditor.cpp` |
| change how a repeat sounds | `RepeatVoice` and `RepeatParams` in `Skipfiend.h` |
| change timing or quantise | `gridNames()` / `gridBeats()` in `Skipfiend.h`; quantise logic in `processBlock()` |
| change random or chaos behaviour | `pickEngineWeighted()`, `randomizeSkipParams()`, the chaos section of `processBlock()` |
| change the sequencer | `seqEngine` / `seqRepeats` arrays and the Skip Language section of `processBlock()`, UI in `SeqCell` |
| change the sidechain trigger | sidechain section of `processBlock()` (the `scTrigger` and `scThresh` params) |
| change MIDI behaviour | `processBlock()` MIDI section, `handleMidiCC()`, `startMidiLearn()` |
| add a factory preset | `getFactoryPresetNames()` and `loadFactoryPreset()` in `PluginProcessor.cpp` |
| change the look | `FiendLNF` in `PluginEditor.cpp`, colour tokens in `namespace col`, palette rules in `theme.md` |
| change the in-plugin help | the `kManualText` raw string in `PluginEditor.cpp` (about line 82 to 337, the Help overlay shows it). It is not read from `MANUAL.md` at runtime, so update both by hand |
| change the export format | `exportAudioToWav()` in `PluginProcessor.cpp`, UI in `exportAudioViaChooser()` |
| change where files go | `skipfiendDir()` and `getDiagnosticsDir()` in `PluginProcessor.cpp` |
| change brand, homepage, support strings | the top of `CMakeLists.txt` (single source of truth) |
| change the plugin ID or CLAP ID | `juce_add_plugin` and `clap_juce_extensions_plugin` in `CMakeLists.txt` (`Fnda`, `Skpf`, `com.circuitdriftlabs.skipfiend`). Changing these breaks saved sessions and host recognition |
| add a test | `Tests/HeadlessTests.cpp` |
| change CI or release | `.github/workflows/ci.yml`, `.github/workflows/release.yml` |
| change the wiki | `docs/wiki/`, see [[Developer-Build-and-Test]] |

## Gotchas for navigation

- `PluginProcessor.cpp` and `PluginEditor.cpp` are large (about 94 KB and 130 KB). Use `grep -n` on the symbol name, do not read them top to bottom.
- Engine IDs are used as array indexes all over the place. Adding one means touching every table, not just the enum.
- `Skipfiend.h` is header-only and included from both processor and editor. It pulls in `juce_audio_basics` and `juce_graphics` (for the engine colours), so do not add GUI-only modules to it.

See [[Developer-Conventions]] for the rules these files follow.
