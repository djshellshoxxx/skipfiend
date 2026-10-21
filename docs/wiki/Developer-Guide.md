# Developer Guide

## Requirements and configure

The project uses CMake 3.22 or newer and C++17. JUCE 8.0.4 is fetched on first configure unless `SKIPFIEND_JUCE_PATH` points to a local checkout. CLAP support is enabled by default and fetches the pinned clap-juce-extensions revision; use `-DSKIPFIEND_BUILD_CLAP=OFF` to omit it.

### Windows (Visual Studio 2022)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Linux or macOS

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The plugin and standalone outputs are placed under `build/SKIPFIEND_artefacts/Release/` (or the chosen configuration directory). CMake is configured to copy the plugin after build by default; set `-DSKIPFIEND_COPY_PLUGIN_AFTER_BUILD=OFF` to disable that behavior.

For memory-limited systems, keep builds single-threaded where supported; LTO is disabled in the project configuration to reduce linker memory use. First-time configure needs network unless JUCE and CLAP dependencies are already available locally.

## Build the headless test harness

Tests are off by default and are not part of the shipped plugin. Configure with:

```sh
cmake -S . -B build -DSKIPFIEND_BUILD_TESTS=ON
cmake --build build --target SKIPFIEND_Tests --config Release
```

Run the resulting `SKIPFIEND_Tests` executable, or use CTest:

```sh
ctest --test-dir build -C Release --output-on-failure
```

The harness drives DSP and state without a DAW or audio device. It covers engine/mode/parameter extremes, state and preset round trips, MIDI learn, sample loading, export, and headless UI renders at different scales and states. These tests complement real-host testing; they do not replace the external pluginval/host validation gate.

## Source map

- `Source/Skipfiend.h`: rolling buffer, repeat voice parameters and processing, recovery artifacts.
- `Source/PluginProcessor.cpp/.h`: parameters, transport/grid, sequencer, sidechain and MIDI triggers, voice pool, state, capture, MIDI export.
- `Source/PluginEditor.cpp/.h`: UI, engine controls, waveform, performance strip, Help/Options/Debug overlays.
- `Tests/HeadlessTests.cpp`: optional headless harness.
- `CMakeLists.txt`: dependencies, formats, options, definitions, and targets.
- `docs/SKIPFIEND_PRODUCT_SPEC.md`: detailed feature and implementation contract.
- `docs/standards/`: shared plugin standard and project-specific development references.

## Implementation guidance

Keep real-time processing bounded. Do not add filesystem I/O, state serialization, unbounded allocation, mutex waits, or other blocking work to `processBlock`. Move capture, MIDI export, preset/file operations, and other UI work to appropriate non-audio threads. Preserve stable parameter IDs because hosts and presets depend on them.

When changing behavior, update the product spec and manual, add headless tests for normal and adversarial cases, and verify parameter/state round trips. When changing visuals, update theme guidance if needed and preserve the headless render checks across scales and overlays. See [Architecture and Real-Time Design](Architecture-and-Real-Time-Design.md) and the [shared baseline](../standards/CDL_PLUGIN_BASELINE.md).