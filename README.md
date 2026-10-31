# Tetorica SPU Plugin

PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native.
Initial development targets macOS Standalone, AUv2 and VST3. Not a published release.

Current prototype: original built-in Sine, MIDI Note On/Off and velocity,
24 voices, channel-aware note ownership, Pitch Bend, Sustain, All sound off,
Gain, custom ADSR editing and mouse keyboard.
Load a complete VAB from the editor and select a Program and Tone to audition.
The bank, selection, Gain and ADSR settings are embedded in the DAW project state.
Additional SPU controls remain planned. The same VAB parser and selected-Tone
engine can also be exercised with the offline tool below.

The goal is to bring the Web SPU Synth's sound-design features to the native
plugin in stages. See the [feature parity plan](docs/spu_synth_parity_plan.md)
for what is implemented, what comes next and which controls belong to the DAW.

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

## MIDI performance

All 16 MIDI channels have independent Pitch Bend and Sustain state.
Pitch Bend is 14-bit, centered at 8192, with a fixed range of **±2 semitones**.
It updates held notes and release tails without restarting samples and also
affects subsequent notes. The SPU's PITCH register still limits the available
range for a given sample. RPN bend-range changes are not implemented yet.

CC64 values 64–127 hold released keys; values 0–63 release only those deferred
notes, leaving physically held keys playing. Sustain postpones Note Off rather
than extending a one-shot sample or overriding its authored ADSR.
CC123 acts as releasing the channel's keys and respects Sustain. CC121 returns
the channel's bend to center and releases Sustain. CC120 currently silences all
channels, as does the editor's **All sound off** button.

MIDI offsets are honored inside blocks, including multiple controller points
in a VST3 block. The plugin supplies a bounded adapter for mapped VST3 MIDI CCs
without changing its pinned framework. Normal Gain and bypass parameters
continue through the framework, along with the ADSR parameters.

Wheel/pedal positions are live MIDI input, not preset data. UI bank/Tone changes
retain them for subsequent notes; successful project-state restore, audio reset,
deactivation and MIDI overflow recovery return them to neutral. Invalid state
restore leaves the existing instrument and controller state intact.
VST3 bypass also clears held/deferred notes and controllers, so input from
bypassed blocks is not replayed when processing resumes.

## ADSR editing

The default **Tone ADSR** mode uses the built-in instrument's or selected VAB
Tone's original envelope. **Copy tone ADSR** copies that envelope into the
editor and enables **Custom ADSR**. You can also enable Custom directly to use
the previously edited values. Custom values stay in place across bank/Program/
Tone changes; switching back to Tone ADSR restores that source's envelope.

Attack, Decay, Sustain level, Sustain rate and Release use the SPU's raw codes.
Rate zero is fastest; larger codes slow the envelope or hold it. Sustain level
is `(code + 1) / 16`, so level zero is not silence. Attack, Sustain and Release
have linear/exponential modes; Sustain can rise or fall. Decay is exponential.
The graph previews the custom envelope's shape, not measured time. Drag A, D,
S or R horizontally to change a rate, and D vertically to change Sustain level.
The knobs also accept typed values. Controls are dimmed while Tone ADSR plays.

**Apply to sounding notes** starts On. Changes keep each note's current level
and sample position, including pedal-held notes and release tails. They do not
restart samples or trigger a new attack. With Apply Off, existing notes retain
their ADSR through Release and new Note Ons use the edited values. Turning
Apply back On immediately applies the latest settings to sounding notes.
Unlike the current Web editor, the On toggle itself performs this update.
Release codes 27–31 hold indefinitely in this core; **All sound off** stops them.

All ADSR controls are host parameters; Gain retains parameter ID 0. Automation
is applied at audio-block boundaries, using the framework's final parameter
point for each block. MIDI Note/Bend/Sustain offsets remain sample-accurate.
Envelope edits write registers and refresh the native envelope without sample
uploads, engine replacement or application-level audio-thread allocations.

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
flags are silently rewritten. See [TODO](docs/issues/TODO.md) for remaining work.

## VAB in the plugin editor

Choose **Load VAB** and open a standard v6/v7 complete `.vab` file (up to 1 MiB).
Reading, parsing and sample-cache preparation run on a worker. Initial loading
selects the first playable Tone. **Program** chooses the first playable Tone
within that Program; **Tone** chooses an exact entry. Both menus use the bank's
original IDs. This auditions one Tone at a time.
**Built-in Sine** returns to the original instrument.

The status distinguishes preparation from audio adoption. A prepared instrument
can wait for the next audio block when processing is stopped. A failed load keeps
the current sound and selection; cancelling the file dialog keeps them too.
The editor shows the embedded bank after project restoration without reopening
the source file. Split `.vh` + `.vb` file selection remains follow-up work.

The prepared-engine exchange is now connected at plugin block boundaries.
`src/VabLoader.h` supplies a non-audio-thread API to prepare a complete VAB Tone
on a worker. A newer request supersedes earlier work; parse/preparation failure
keeps the current instrument. The audio side adopts prepared engines without
allocating, destroying engines, taking an application mutex or notifying a
worker. Old engines return through a bounded retirement queue; a full queue
defers the switch. This layer does not make the entire host/framework callback
lock-free. Each plugin instance owns its loader and joins it before destroying
the engine exchange.

## Embedded DAW state

Custom state now embeds the complete original VAB, sparse Program/Tone selection
and Gain, custom ADSR and Apply settings. Restoring a project prepares a fresh SPU synchronously before reporting
success; it does not need an external sample path. Malformed or unsupported state
keeps the current sound, parameters and an already prepared pending replacement.
Previous Gain-only AU/VST3 state remains readable and migrates to Built-in Sine.
Schema 1 VAB/Gain states also remain readable and use their original Tone ADSR.
Schema 2 keeps the original bank bytes and adds the custom ADSR settings.

The actual AU and VST3 have been tested for embedded-bank round-trip, separate
instances, immediate first-render MIDI/PCM and malformed-state rejection. VST3
also covers held/deferred MIDI cleanup and repeated restores at 44.1/48/96 kHz.
See [the state verification record](docs/embedded_state_20261031.md).

```sh
sh scripts/check_au_state.sh build/macos/out/TetoricaSPU.component
sh scripts/check_vst3_audio.sh build/macos/out/TetoricaSPU.vst3
```

These hosts load the workspace bundles without opening a device or editor.
State restore is a non-audio-thread host lifecycle operation, with processing
quiescent during the call. A new instance starts with Built-in Sine; reopening
the editor after restoration displays the saved bank and Program/Tone.
See [the VAB UI verification record](docs/vab_ui_20261031.md) for checks completed
and remaining DAW-specific validation.

## macOS AU / GarageBand verification

```sh
python3 scripts/install_au.py           # Inspect; no install unless requested
python3 scripts/install_au.py --install # New user-only AU; refuses differing existing bundles
python3 scripts/validate_au.py         # Wait for discovery, then run auval
```

An explicit development update can replace a known previous AU after all audio
hosts have quit. It verifies the previous record, retains full backups outside
Components, stages the new bundle, and records a conditional rollback command.
The ordinary `--install` still refuses a different existing bundle.

```sh
python3 -m unittest discover -s tests -p 'test_install_au.py' -v
python3 scripts/install_au.py --update \
  --expected docs/midi_expression_20261031.json --backup-root build/au-backups
# Use the printed manifest path to roll back; refuses an altered installation:
python3 scripts/install_au.py --rollback build/au-backups/au-update-REPLACE/manifest.json
```

The [MIDI verification record](docs/midi_expression_20261031.md) contains the
current installed manifest, backup report and GarageBand restoration result.

The destination is `~/Library/Audio/Plug-Ins/Components/TetoricaSPU.component`.
This is an explicit test installation; the normal build still does not deploy.
GarageBand lists it under **AU Instruments → Tetorica → TetoricaSPU → Stereo**.
Local AU validation and a GarageBand MIDI-record/save/reopen/WAVE-export workflow
were completed; physical listening and long realtime testing remain separate.
See [the AU / GarageBand record](docs/au_garageband_20261031.md).

The [VAB GarageBand check](docs/au_vab_garageband_20261031.md) used the host's
standard AU preset loader to load an embedded VAB, save a copied project, quit,
and reopen without the loaded preset file. Program/Tone/Gain were restored and
the before/after exported PCM matched exactly. The original project and old AU
were preserved. Remote AU custom-control clicks and Tone-only dirty behavior
still need a direct user check; the verified host-preset route is separate.

## Prototype limits

- The fixed 112-frame Sine reaches the SPU PITCH ceiling at about 1,575 Hz;
  MIDI notes 92 and above clamp. Full-range multisampling is follow-up work.
- Host rate conversion uses continuous linear interpolation. Anti-alias filtering
  below 44.1 kHz and improved conversion quality remain follow-up work.
- State schema 1 stores a built-in instrument or one selected VAB Tone, with
  a 1 MiB bank limit. Key zones, layers and game-driver performance state are
  not reconstructed. Held notes, release tails, decoder caches and host rate
  are not saved.
- Pitch Bend, Sustain, smooth Gain changes and per-channel CC120 remain TODO.
  Currently CC120 immediately stops all channels; CC123 releases its own channel.
- Prepared-engine changes start with no held notes and cut the previous tail.
  Carried MIDI from previous blocks is removed; new current-block host MIDI is
  retained. Tail crossfades and universal non-render-thread AU MIDI ingress are
  follow-up work.
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
