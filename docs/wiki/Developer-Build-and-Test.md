# Developer Build and Test

## Toolchain

- CMake 3.22 or newer (`cmake_minimum_required(VERSION 3.22)`)
- C++17 compiler: MSVC (Visual Studio 2022) on Windows, Clang or GCC on macOS and Linux
- JUCE 8.0.4, fetched at configure time via `FetchContent` from `https://github.com/juce-framework/JUCE.git`. Needs network on the first configure.
- CLAP support via `clap-juce-extensions` (pinned commit in CMake, fetched with submodules). Disable with `-DSKIPFIEND_BUILD_CLAP=OFF`.
- Linux only: ALSA, FreeType, fontconfig, Mesa GL, X11 (xext, xinerama, xcursor, xrandr) and GTK 3 dev packages. The CI workflow lists them under "Install JUCE Linux dependencies".

## Build

Windows (Visual Studio 2022):

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

macOS and Linux:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs go to `build/SKIPFIEND_artefacts/Release/` (VST3, CLAP, Standalone). `SKIPFIEND_COPY_PLUGIN_AFTER_BUILD` (default ON) also installs the built VST3 into the system plugin folder. CI turns it off.

On a memory-constrained machine, build single threaded:

```
cmake --build build --config Release -- -m:1
```

### CMake options

| Option | Default | What it does |
|---|---|---|
| `SKIPFIEND_JUCE_PATH` | empty | use a local JUCE checkout instead of fetching 8.0.4 |
| `SKIPFIEND_BUILD_CLAP` | ON | build the CLAP format |
| `SKIPFIEND_BUILD_TESTS` | OFF | build `SKIPFIEND_Tests` (never part of a plugin build) |
| `SKIPFIEND_COPY_PLUGIN_AFTER_BUILD` | ON | install the VST3 after build |
| `SKIPFIEND_HOMEPAGE`, `SKIPFIEND_SUPPORT_CONTACT`, `SKIPFIEND_GITHUB` | set in CMake | strings shown in the host, About box and troubleshooting export. Replace before release |

LTO is deliberately disabled (`juce_recommended_lto_flags` is commented out). The link step is very RAM hungry. Turn it back on only on a machine with plenty of memory.

## Tests

The headless harness is `Tests/HeadlessTests.cpp`. It drives the processor with no host and no audio device, covering every engine, every mode, parameters at both extremes, presets, randomise, A/B, state round trips, MIDI learn, the sample deck and export. It checks that output stays finite and in range after every block. It also builds the editor with no window and renders PNGs (main panel, overlays, live states) to the temp directory as `skipfiend_ui*.png`.

```
cmake -S . -B build -DSKIPFIEND_BUILD_TESTS=ON
cmake --build build --target SKIPFIEND_Tests --config Release -- -m:1
./build/SKIPFIEND_Tests_artefacts/Release/SKIPFIEND_Tests
```

It prints PASS or FAIL per case and exits non-zero on any failure. It also registers with CTest as `skipfiend_headless`:

```
ctest --test-dir build --output-on-failure -C Release
```

## CI (`.github/workflows/ci.yml`)

- Triggers: push to `main` and `audit/**`, and pull requests into `main`.
- One job, `build-and-test`, on `ubuntu-latest`, 20 minute timeout.
- Installs the Linux JUCE dependencies, configures with `-DCMAKE_BUILD_TYPE=Release -DSKIPFIEND_BUILD_TESTS=ON -DSKIPFIEND_COPY_PLUGIN_AFTER_BUILD=OFF`.
- Builds `SKIPFIEND_VST3`, `SKIPFIEND_CLAP`, `SKIPFIEND_Standalone` and then `SKIPFIEND_Tests` with `--parallel 2`.
- Runs `ctest --test-dir build --output-on-failure -C Release`.

## Release (`.github/workflows/release.yml`)

- Triggers: pushes of tags matching `v*`, and pushes to `main` where the version in `CMakeLists.txt` has no matching release yet. Bump `project(SKIPFIEND VERSION ...)` to publish a new version.
- The `version` job computes the tag as `v<project version>` and stops if that release already exists.
- Matrix: `Windows-x64` and `Linux-x64`. Each one configures, builds the Standalone, VST3, CLAP and Tests targets, runs ctest, packages the artefacts, and uploads them.
- Assets per release: `SKIPFIEND-<tag>-Windows-x64.exe` (standalone), `SKIPFIEND-<tag>-Windows-x64.zip` (exe, VST3, CLAP), `SKIPFIEND-<tag>-Linux-x64.tar.gz` (standalone, VST3, CLAP).
- The `release` job downloads the artefacts and publishes the GitHub Release with `softprops/action-gh-release@v2`. Builds are unsigned.

## Wiki sync (`.github/workflows/wiki-sync.yml`)

Pages in `docs/wiki/` are copied to the GitHub wiki repo (`<repo>.wiki.git`) by `wiki-sync.yml`. It runs on pushes to `main` that touch `docs/wiki/**`, and on manual dispatch. It needs `contents: write`. If the wiki repo does not exist yet, the job exits with a notice instead of failing. To create it the first time, open the repo's Wiki tab and save any page once.

## Version

The version is `project(SKIPFIEND VERSION 1.0.0 ...)` in `CMakeLists.txt`. It feeds `SKIPFIEND_VERSION`, the VST3 module info and the About box.

## Troubleshooting builds

- Fetch fails on first configure: no network, or GitHub blocked. Use `-DSKIPFIEND_JUCE_PATH=/path/to/JUCE`.
- Linker killed or very slow: build with `-- -m:1`, close other apps, keep LTO off.
- Plugin not picked up by the host after a local build: the copy step only runs with `SKIPFIEND_COPY_PLUGIN_AFTER_BUILD=ON`, and the host may need a rescan.
