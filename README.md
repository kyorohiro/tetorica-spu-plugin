# Tetorica SPU Plugin

PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native.
Initial development targets macOS Standalone, AUv2 and VST3. Not a published release.

The audio engine runs directly as native C++ in the host callback. It does not
send audio through a browser, WebView, Tauri IPC or a separate CPAL device.

## Resume work

Read [TODO](docs/issues/TODO.md) and [checkpoints](docs/progress.md) first.
Dependency revisions are recorded both as Git submodules and in
[dependencies.lock.json](dependencies.lock.json).

## Setup

Requires Git, Python 3, CMake 3.25+, a C++17 compiler and Xcode for macOS bundles.

```sh
python3 scripts/bootstrap.py
python3 scripts/bootstrap.py --check
```

The script resumes Git submodule initialization without moving the pinned
revisions. Only required VST3 SDK submodules are initialized. Do not run the
upstream script that downloads SDKs from moving branches.

## Build and automated checks

```sh
cmake --preset offline
cmake --build --preset offline --parallel 4
ctest --preset offline

cmake --preset macos
cmake --build --preset macos --parallel 4
ctest --preset macos
```

macOS bundles are written to `build/macos/out/`. Automatic installation into
`~/Applications` or `~/Library/Audio/Plug-Ins` is disabled. Host registration,
DAW tests and physical listening are separate from a successful build or CTest.

## Dependencies and licenses

- [iPlug2OOS](https://github.com/iPlug2/iPlug2OOS) is the starting template.
  iPlug2 uses its [zlib-like license](vendor/iPlug2OOS/iPlug2/LICENSE.txt).
- [tetorica-soundchip-native](https://github.com/kyorohiro/tetorica-soundchip-native)
  supplies the C++ PS1 SPU, using the BSD-3-Clause MAME-derived port. See its
  [license and provenance](vendor/tetorica-soundchip-native/vendor/mame-ps1-spu/README.md).
  This project pins a public commit after SDK v0.1.6 to retain SPU fixes; it does
  not claim that an SDK v0.1.7 release has been published.
- [VST3 SDK](https://github.com/steinbergmedia/vst3sdk) is pinned to
  `v3.8.0_build_66`. Preserve its license files when redistributing sources.
- Template resource/font notices remain with their copied resources.

Plugin IDs and project version are development identifiers; release, signing
and notarization are not performed by the default build.
