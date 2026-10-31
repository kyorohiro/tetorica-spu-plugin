# Tetorica SPU Plugin

PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native.
Initial development targets macOS Standalone, AUv2 and VST3. Not a published release.

Current prototype: original built-in Sine, MIDI Note On/Off and velocity,
24 voices, channel-aware note ownership, All sound off, Gain and mouse keyboard.
VAB file selection in the plugin and additional SPU controls remain planned.
The UI-independent VAB parser and selected-Tone engine can now be exercised
with the offline tool below.

The audio engine runs directly as native C++ in the host callback. It does not
send audio through a browser, WebView, Tauri IPC or a separate CPAL device.

## Resume work

Read [TODO](docs/issues/TODO.md) and [checkpoints](docs/progress.md) first.
The [initial verification record](docs/verification_20261031.json) separates
offline/native-plugin tests from pending DAW and physical listening checks.
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

```sh
sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
build/offline/spu_instrument_test --wav build/spu-demo.wav
```

The VST3 checks load the actual bundle without registering it with a DAW or
opening an audio device. The WAV demo contains an original C/E/G chord and release.

## VAB offline preview

```sh
build/offline/spu_vab_render tests/fixtures/vab/spu-synth-demo.vab
build/offline/spu_vab_render tests/fixtures/vab/spu-synth-demo.vab 0 build/vab-tone-0.wav
```

The first command lists sparse Program IDs and flattened Tone indices. The
second renders the selected Tone at MIDI note 60, with one second held and one
second of release, to a new 48 kHz stereo PCM16 WAV. Existing output files are
not overwritten. Use your own standard v6/v7 complete VAB in place of the
original synthetic fixture. This preview does not open an audio device.

`src/VabBank.h` also accepts an explicitly supplied VH + VB pair. Parsing owns
its data and selected-Tone preparation retains original ADPCM through its first
END, tuning, ADSR, loop position and approximate static volume/pan. A fresh
`SpuInstrument(preparedTone)` primes caches before it can enter a render callback.
Imported reverb send is retained; the shared Reverb is still off.

This is selected-Tone audition, not song playback or automatic key-zone/layer
reconstruction. Sample 0 (unassigned), sample 255 (Noise), malformed ADPCM and
selected payloads beyond usable SPU RAM are rejected. The pinned Native SDK
also requires the repeat-address packet to carry flag 2; structurally valid
loops without it, including flag 4 alone, are rejected for playback. No sample
flags are silently rewritten. File UI, live engine replacement and embedded
VAB state in DAW projects are the next integration steps; see [TODO](docs/issues/TODO.md).

## macOS AU / GarageBand verification

```sh
python3 scripts/install_au.py           # Inspect; no install unless requested
python3 scripts/install_au.py --install # New user-only AU; refuses differing existing bundles
python3 scripts/validate_au.py         # Wait for discovery, then run auval
```

The destination is `~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`.
This is an explicit test installation; the normal build still does not deploy.
GarageBand lists it under **AU Instruments → Tetorica → TetoricaSPU → Stereo**.
Local AU validation and a GarageBand MIDI-record/save/reopen/WAVE-export workflow
were completed; physical listening and long realtime testing remain separate.
See [the AU / GarageBand record](docs/au_garageband_20261031.md).

## Prototype limits

- The fixed 112-frame Sine reaches the SPU PITCH ceiling at about 1,575 Hz;
  MIDI notes 92 and above clamp. Full-range multisampling is follow-up work.
- Host rate conversion uses continuous linear interpolation. Anti-alias filtering
  below 44.1 kHz and improved conversion quality remain follow-up work.
- Gain parameter state is supplied by iPlug2; custom VAB/sample state is not implemented.
- Pitch Bend, Sustain, smooth Gain changes and per-channel CC120 remain TODO.
  Currently CC120 immediately stops all channels; CC123 releases its own channel.
- Sample caches are prepared during construction. The offline tests
  probes C++ new/delete during rendering, not every OS allocation or hard real-time deadline.

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
