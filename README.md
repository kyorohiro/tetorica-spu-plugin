# Tetorica SPU Plugin

PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native.
Targets macOS Universal Standalone, AUv2 and VST3, plus Windows x64 VST3.

[Download v0.0.0 Preview](https://github.com/kyorohiro/tetorica-spu-plugin/releases/tag/v0.0.0):
macOS 13+ Universal Developer ID signed and notarized DMG (APP / AUv2 / VST3),
or unsigned Windows x64 VST3 ZIP. See the
[release verification record and remaining checks](docs/release_v0_0_0_20261102.md).

Current prototype: full-range built-in Sine with legacy project playback, MIDI Note On/Off and velocity,
24 voices, channel-aware note ownership, Pitch Bend, Sustain, All sound off,
Gain, a PCM waveform editor, Software Morph, SPU Noise, paired PMON, custom ADSR editing, shared SPU Reverb and mouse keyboard.
Load a complete VAB from the editor and select a Program and Tone to audition.
The bank, PCM cycle or imported ADPCM / Software PCM, Morph A/B endpoints, selection, Gain, ADSR, Reverb, Noise and PMON settings are embedded in the DAW project state.
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

## Preview candidates

[Build Preview Candidates](https://github.com/kyorohiro/tetorica-spu-plugin/actions/workflows/preview.yml)
produces ZIPs and SHA256 files in Actions artifacts. These are development
candidates; no tag or public GitHub Release is created automatically.

| Target | Candidate | Build and checks |
| --- | --- | --- |
| macOS 13+ | Universal APP / AUv2 / VST3, arm64 + x86_64 | Built on Mac; the same packaged AU/VST3 is checked on Apple Silicon and Intel runners |
| Windows x64 | VST3 bundle | Built, rendered and validated on windows-2022 |
| Linux | No DAW plugin candidate yet | Ubuntu core / offline renderer tests continue separately |

The pinned iPlug2 Linux UI/APP/VST3 implementation is incomplete. Linux plugin
work is a separate TODO; a passing Linux core test is not a plugin release.
Windows Standalone compiles for internal checking but is excluded from the ZIP:
its pinned ASIO source refers to separate SDK terms absent from the checkout.
The VST3 does not link those APP/ASIO sources and uses the DAW's audio driver.

For a local Mac candidate:

```sh
cmake --preset macos-release
cmake --build --preset macos-release --parallel 4
ctest --preset macos-release
python3 scripts/package_preview.py build --platform macos --input build/macos-release/out --output release --ad-hoc-sign --report build/macos-preview.json
python3 scripts/verify_preview_hosts.py --report build/macos-preview.json
```

Commit changes before packaging: each manifest records an exact source commit.
Only staging copies are signed. The ZIP command above produces ad-hoc signatures,
without Developer ID signing or notarization; Windows candidates are unsigned.
ZIP verification checks the manifest, SHA256, executable permissions and safe
paths. Each ZIP includes installation notes and third-party license notices.
Extract safely with `python3 scripts/package_preview.py verify CANDIDATE.zip --extract-to build/preview`.
The host-check command above does not install an AU or open an audio device.
CI alone enables the extra ephemeral AU installation/auval step.

On Windows, install Visual Studio 2022 with Desktop development with C++ and a
Windows SDK. Build and package on Windows; the Mac workflow does not attempt a
Windows cross-build:

```sh
cmake --preset windows
cmake --build --preset windows --parallel 4
ctest --preset windows
python scripts/package_preview.py build --platform windows --input build/windows/out --output release --report build/windows-preview.json
python scripts/verify_preview_hosts.py --report build/windows-preview.json
```

See [candidate verification and remaining checks](docs/preview_builds_20261102.md).

For Developer ID distribution, [Build Signed macOS Candidate](https://github.com/kyorohiro/tetorica-spu-plugin/actions/workflows/macos-signed.yml)
is a separate manual workflow restricted to `main`. It signs Universal staging
copies with hardened runtime and trusted timestamps, submits the signed DMG to
Apple, requires an Accepted result, and staples and validates the DMG ticket.
Apple Silicon and Intel jobs download that same DMG, check its signature and
Gatekeeper acceptance, extract its versioned payload folder read-only, and test
the actual AU/VST3. This workflow creates artifacts without creating a tag or Release.

It reads these five repository Actions secrets: `BUILD_CERTIFICATE_BASE64`,
`P12_PASSWORD`, `APPLE_ID`, `APPLE_TEAM_ID`, and `APPLE_APP_SPECIFIC_PASSWORD`.
The certificate must include its private key and be Developer ID Application
for the expected public Team ID in the workflow. Credentials use a temporary
file-based keychain and are removed by an `always()` cleanup step.

Open the DMG's versioned folder for the APP, AU, VST3, installation notes,
licenses and signed-payload manifest. Keep plugin bundles intact when copying
them to the installation paths described in `INSTALL.txt`.

## MIDI performance

All 16 MIDI channels have independent Pitch Bend and Sustain state.
Pitch Bend is 14-bit, centered at 8192, with a fixed range of **±2 semitones**.
It updates held notes and release tails without restarting samples and also
affects subsequent notes. The SPU's PITCH register still limits the available
range for a given sample. RPN bend-range changes are not implemented yet.

New instances and the **Builtin Sine** button use a five-zone, software-authored
ADPCM Sine covering MIDI 0–127 with ±2-semitone Bend at normal 44.1 kHz or higher
host rates. Each key reserves upward Bend headroom when choosing its zone;
held-note bends change only PITCH, retaining phase and ADSR. The original
112-frame source is used for keys 0–89, then 2/4/8/16-period sources for
90–101 / 102–113 / 114–125 / 126–127. SPU Gaussian interpolation, ADSR, PMON and
Reverb still supply the sound. Zone quantization and timbre can differ.

Old Builtin projects restore as **Builtin Sine (legacy)**, preserving the original
sample, transitions and high-note ceiling. Clicking **Builtin Sine** explicitly
selects the new source. Authored PCM, Morph, VAB and the PMON modulator retain
their existing sample-dependent PITCH limits; PMON's Builtin carrier can use the
new zones. Web export contains the original single Sine sample and explains the
lost high-range mapping in the export-details tooltip. Reimporting that file
does not recover the multi-zone source. See [range and compatibility verification](docs/builtin_sine_range_20261102.md).

CC64 values 64–127 hold released keys; values 0–63 release only those deferred
notes, leaving physically held keys playing. Sustain postpones Note Off rather
than extending a one-shot sample or overriding its authored ADSR.
CC123 acts as releasing the channel's keys and respects Sustain. CC121 returns
the channel's bend to center and releases Sustain. **CC120 mutes only the
addressed MIDI channel**, including pedal-held notes and release tails, and
stops both voices of each PMON pair. Bend/pedal settings, other channels and the
shared Reverb tail are retained. The causal rate adapter retains its existing
mixed-sample history for up to about two native sample intervals.

The editor's **All sound off** button still stops every channel and clears the
shared Reverb tail. Editor notes and this global stop share one bounded FIFO,
so older UI NoteOns cannot arrive after their stop. Pending notes before the
stop are discarded; subsequent UI NoteOns can play. Queue overflow safely stops
all voices and resets controllers. This UI path has one editor producer and
one audio consumer; host MIDI retains its existing serialized-ingress contract.

See the [CC120 verification and deployment status](docs/cc120_20261101.md).

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

Gain changes use an approximately **5 ms linear ramp** after the SPU output,
with one shared coefficient per stereo frame. The ramp continues across audio
blocks and MIDI offsets. This is plugin output smoothing, separate from the
SPU's voice volumes and ADSR. Normal host Gain automation still supplies one
target per block; individual automation-point offsets are not scheduled.
Initial playback, audio reset, reactivation and successful state/import restore
start at the saved Gain immediately. Only the target is saved; an unfinished
ramp is live processing state. Gain remains 0–100%, parameter ID 0.
See [Gain smoothing verification](docs/gain_smoothing_20261101.md) for the
actual AU/VST3 PCM checks and deployment status.

## PCM waveform editing

Open the **Source** page with **Source: Sample** to choose Sine, Triangle, Saw or Square, or draw one cycle
on the single canvas. The editor uses 112 signed PCM samples in `[-1, 1]`.
The four shapes have amplitude 0.8, matching the original built-in Sine.
Edits are a draft until **Apply PCM**; **Revert** discards the draft.
Unapplied changes are not included in project state.

Applying prepares a new instrument on the worker, then exchanges it at the next
audio block. Held notes and old reverb tails stop at that exchange; play a new
note to hear the new cycle. Gain, custom ADSR, Reverb and live wheel/pedal values
are retained. Draft editing never writes sample RAM on the audio thread.

This is a **software authoring feature**: Tetorica converts the drawn PCM to four
looping SPU ADPCM packets. The SPU plays the encoded result with its normal
quantization and Gaussian interpolation. The cyclic encoder scores the second
decode pass to avoid the flattened first packet of independent encoding.
It does not replace ADPCM with host PCM. PCM sound definitions have no original
Tone reverb-send flag; use **Send: all notes** to send them to Reverb.

Applied cycles are stored as 112 Float32 samples in the project and encoded
again when restored. No external file is needed. Shape selection and drawing
are instrument edits rather than host automation parameters; the existing
17 pre-existing parameter IDs remain unchanged. **Builtin Sine** selects the full-range
multisample source. Editing a VAB does not rewrite its sample bank: Apply replaces
the current source with the authored PCM cycle.

## Software Morph

On the **Source** page with **Sample** selected, enable **Software Morph**.
The original Builtin/VAB/PCM source is retained. Two 112-sample cycles, **Wave A**
and **Wave B**, blend through **Morph Amount** (0–1). A starts as Sine and B as
Saw, using the editor's amplitude 0.8. Amount 0 selects A; 1 selects B.
Morph starts Off. Turning it Off returns to the retained original sample.
Changing enabled mode stops held notes and old Reverb tails; play a new note.

**A, B and Result share one canvas.** Select A or B to draw that endpoint or
choose a shape. Result is the blend preview and cannot be drawn directly.
Endpoint edits remain a draft until **Apply**; **Revert** discards the draft.
Apply prepares a replacement on the worker and stops old notes/tails at adoption.
Unapplied endpoint changes are not saved. The ordinary PCM draft is separate.

Amount changes the applied A/B blend while notes, sustain or release tails sound,
without restarting sample positions or envelopes. The Source control and the
control beside the keyboard use the same host parameter, so both follow edits,
automation and restore. When a draft is unapplied, the canvas previews that
draft while sounding notes still use the applied endpoints.

Morph works with **Sample + PMON**, retaining the paired 12-note limit.
**SPU Noise** takes priority and keeps Morph settings for returning to Sample.
Source changes retain the applied endpoints and Morph parameters; turn Morph
Off to hear the selected base sample. Carrier ADSR, volume/pan and Tone-send
remain from that base; Morph uses tuning for a 112-sample cycle.

This is a **software feature**, using a second private PCM bank outside physical
SPU sample RAM. The SPU still supplies Gaussian interpolation, ADSR, PMON and
Reverb. Morph's carrier uses signed 16-bit PCM; the ordinary sample uses SPU
ADPCM. Enabling Morph is therefore not a promise of bit-identical ADPCM sound.
Morph Off keeps the previous source playback unchanged.

Applied A/B originals are embedded in project state. Enabled and Amount append
host parameter IDs 24 and 25; previous IDs stay stable. This plugin uses explicit
Apply for endpoint edits and returns to its base source on Morph Off. The Web
editor instead applies completed endpoint edits live and keeps the last blend
when Morph is turned Off. Morph LFO and external-input effects are not included.

## SPU Noise

On the **Source** page, switch **Source: Sample** to **Source: SPU Noise**.
Keys trigger the SPU Noise generator through the selected instrument's velocity,
volume/pan and ADSR. Sustain and Reverb remain available. Noise is a shared SPU
function; the PCM drawing editor remains a software authoring feature.

**Shared Noise clock** is a raw code from 0 to 63, initially 48, shared by all
24 voices in this instance. It changes Noise rate/texture while notes or release
tails sound, without restarting envelopes. Note and Pitch Bend do not tune Noise
to a musical scale. Live bend values are still retained for returning to Sample.
The pinned core uses an approximate generator rather than a hardware-exact model.

Changing Sample/Noise mode stops held notes and old Reverb tails. Play a new
note after the switch. The original Builtin/PCM/VAB and any local PCM draft are
kept, so **Source: Sample** returns to that sample. One-shot sample END does not
end a Noise note; its ADSR still controls its duration. Reverb's **Tone setting**
send uses the retained original source flag in both modes.

Noise mode and Clock are saved host parameters, IDs 17 and 18. Automation is
applied at block boundaries. Old projects restore with Noise Off and Clock 48.
This does not automatically reconstruct VAB sample-255 Noise Tones.

## SPU Pitch Modulation (PMON)

On the **Source** page, select **Sample + PMON**, then play a new note.
Each note uses an even modulator voice and its adjacent odd sample carrier,
giving **12 notes** instead of 24. This uses SPU pitch modulation; the generated
modulator PCM and its live updates are software authoring, outside physical
sample RAM. Existing VAB and drawn PCM remain the carrier. Modulators are
silent in the dry mix and Reverb; only carriers reach the output/effect.

- **Modulator** selects Sine, Triangle, Saw or Square.
- **Ratio** sets the modulator frequency relative to the played note (0.01–16).
- **Depth** sets modulation strength (0–1).
- **Decay** approximates the modulator's SPU envelope duration (0–5000 ms).
  **0 means Hold**. It changes the next note; sounding notes keep their latched
  modulator envelope. SPU rate steps make the displayed milliseconds approximate.

Waveform, Ratio and Depth affect sounding notes, including release tails,
without retriggering them. Pitch Bend tunes both voices. Sustain defers release
of both; custom/Tone ADSR edits apply to the carrier. The modulator uses the
carrier's Release setting when its note begins.

Use **Edit carrier (Sample)** to return to PCM editing, then select
**Sample + PMON** again. Mode changes stop notes and Reverb tails and retain
the original sample, PCM draft and PMON settings. Reverb stays on its own page.

The five PMON host parameters append IDs 19–23; previous IDs remain stable.
They apply at block boundaries. Noise and PMON flags are independently saved
for host automation compatibility: if both are On, **Noise takes priority**.
The Source menu sets both flags to choose one mode. Old projects use PMON Off,
Sine, Ratio 1, Depth 0.5 and Decay 500 ms. Arbitrary voice-to-voice routing is
outside this paired instrument's scope.

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

## SPU Reverb

The **Source / ADSR / Reverb** page buttons share the same bank, Gain and keyboard.
Choose a **Shared preset** to enable Reverb automatically: Room, Studio Small,
Studio Medium, Studio Large, Hall, Space Echo or Half Echo. These are the pinned
core's named approximations of PS1 Reverb, not a complete hardware algorithm.
The initial preset is **Off** and Return left/right start at 25%.

The Return knobs add wet sound to the unchanged dry sound. Each output channel
has its own return level. **Send: all notes** sends every keyboard voice to the
shared effect. **Send: Tone setting** follows the original selected VAB Tone's
send flag; Builtin Sine has no original send. The source flag is retained when
changing this control or saving the bank.

Turning **Enable reverb** Off immediately mutes wet output, keeps the selected
preset/return values and lets the effect decay silently. Selecting **Off** also
clears its history. The editor's **All sound off** button, audio reset and
deactivation clear voices and effect history, so old tails cannot return after
restarting. Selecting a different active preset keeps the current voices and
effect history.
This immediate bypass and Off-history clearing differs from the Web editor's
send-only Enable switch. Preset selection auto-enables only through the editor;
DAW automation and project restore retain the independent Enable value.

All five Reverb controls are host parameters, IDs 12–16. Changes apply at block
boundaries without retriggering notes or replacing the instrument. Clearing
effect history uses the Native SDK's allocation-free `ps1_spu_clear_reverb` API;
it preserves sample RAM, registers and warmed ADPCM caches. Bypassed processing
does not repeatedly clear delay buffers on every host block.

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
Imported reverb send is retained; shared Reverb starts Off and can be edited in the plugin.

This is selected-Tone audition, not song playback or automatic key-zone/layer
reconstruction. Sample 0 (unassigned), sample 255 (Noise), malformed ADPCM and
selected payloads beyond usable SPU RAM are rejected. Loop playback follows the END packet's Repeat flag and the original loop-start
marker or explicit repeat address. A loop-start packet does not need Repeat
flag 2. Original ADPCM flags and bytes are preserved. Native plugin tests cover
flag4-only and markerless targets, exact PCM against flag6/2 controls, embedded
state restoration and Web round trips; see the
[loop compatibility verification](docs/loop_repeat_20261101.md). See
[TODO](docs/issues/TODO.md) for remaining work.

## VAB in the plugin editor

Choose **Load sound...** and open a standard v6/v7 complete `.vab` file (up to 1 MiB).
For split banks, select either the `.vh` header or the `.vb` body first, then
select its matching partner in the second file dialog. The second dialog starts
in the first file's directory and filters for the other extension. Cancelling
either dialog keeps the current sound. No load is submitted until both are chosen.
The VH limit is 68,128 bytes and the VB limit is 512 KiB. The parser checks their
declared combined size and sample boundaries; the format has no checksum that
can identify an unrelated body with the same valid layout.

Reading, parsing and sample-cache preparation run on a worker. Initial loading
selects the first playable Tone. **Program** chooses the first playable Tone
within that Program; **Tone** chooses an exact entry. Both menus use the bank's
original IDs. This auditions one Tone at a time.
**Builtin Sine** explicitly selects the full-range multisample instrument.

The status distinguishes preparation from audio adoption. A prepared instrument
can wait for the next audio block when processing is stopped. A failed load keeps
the current sound and selection; cancelling the file dialog keeps them too.
The editor shows the embedded bank after project restoration without reopening
the source file. Split input is embedded as the original VH bytes followed by
the original VB bytes, so both source files can be moved after saving. Program /
Tone selection and selected-Tone Web export work as with a complete VAB.
See the [split VAB and AU preset verification record](docs/preset_split_vab_20261101.md).

The prepared-engine exchange is now connected at plugin block boundaries.
`src/VabLoader.h` supplies a non-audio-thread API to prepare a complete VAB Tone
on a worker. A newer request supersedes earlier work; parse/preparation failure
keeps the current instrument. The audio side adopts prepared engines without
allocating, destroying engines, taking an application mutex or notifying a
worker. Old engines return through a bounded retirement queue; a full queue
defers the switch. This layer does not make the entire host/framework callback
lock-free. Each plugin instance owns its loader and joins it before destroying
the engine exchange.

## Import a Web SPU sound

Save an instrument as `.spu.json` in the Web SPU Synth, then choose
**Load sound...** in the plugin. Version 1 supports original ADPCM samples,
authored 112-sample PCM cycles, Morph A/B, ADSR, shared Reverb, Noise and PMON.
The instrument name appears in the editor. Program/Tone menus apply to VAB banks.
The plugin keeps its current Gain, since Web sound files contain no master Gain.

The original sample bytes, tuning, loop, signed stereo volumes, ADSR and send
are embedded in the DAW project. A prepared import adopts its sound and all
settings together at an audio-block boundary, stopping previous notes and tails.
Invalid files preserve the current sound and valid pending work. Closing the
editor or restoring a project cancels unfinished import preparation.

Version 1 remains bounded to 8 MiB JSON and 520192 ADPCM bytes, and retains original packet flags. Version 2 reads mono PCM16LE
base64, tuning and one-shot / whole-sample loop, up to 32 MiB decoded PCM and
48 MiB JSON. This is a bounded subset of the Web format: larger Web recordings
are rejected. PSF extraction remains follow-up work. Web export is available below.

Web saves the current Morph blend as the base sample; turning Morph Off after
import returns to that saved sample. It cannot recover a pre-Morph VAB absent
from the file. The plugin uses its existing voice-level calibration and private
16-bit Morph carrier, so Web/plugin output PCM is not promised bit-identical.
See the [import verification record](docs/web_instrument_import_20261101.md).

## Load a WAV recording

Choose **Load sound...** and select a `.wav` file. Classic little-endian RIFF/WAVE
PCM16 and IEEE Float32, mono or stereo, at 8–384 kHz are supported. Stereo is
averaged to mono and other rates use a band-limited conversion to 44.1 kHz.
There is no automatic normalization. Float samples must be finite and within
[-1, 1]. Short recordings are encoded as ordinary SPU ADPCM. Recordings that
exceed sample RAM automatically use **Software audio**, mono PCM16 held outside
physical SPU RAM, while retaining SPU Gaussian interpolation, ADSR, PMON and Reverb.

The initial **Root: C4 (MIDI 60)** plays the recording at its original speed;
other keys transpose it. The Root and **Playback: One shot** menus occupy the
Program/Tone row for ADPCM and Software audio. Choose **Loop entire sample**
to repeat the recording. ADPCM includes up to 27 zero padding frames; Software
audio retains the exact converted frame count and loops require at least four frames. Recording boundaries can
produce a loop click. These changes replace the prepared sound and stop held
notes and reverb tails. Morph uses its own cycle tuning.

A successful WAV load selects Sample with Morph Off for the new material.
Gain, ADSR, Reverb and dormant Noise/PMON/Morph settings are retained,
including the Morph endpoints captured when the file was selected. Root and
Playback edits retain the current sound settings. The result is embedded in
the DAW project with its root and loop, so reopening needs no original WAV.
ADPCM is lossy. Software audio retains the converted mono 44.1 kHz PCM16,
without further ADPCM quantization; it is not a copy of the original stereo/rate/float format.

File reading, conversion and native engine preparation run on a worker. A
failed load keeps the current sound and a valid pending change. The input is
bounded to 32 MiB, and converted Software PCM is also bounded to 32 MiB
(16,777,216 mono frames, about 6 minutes 20 seconds). This supports a 20 MiB
44.1 kHz mono PCM16 recording without truncation. Inputs that exceed either bound are rejected.
Compressed WAV, extensible WAV, AIFF and RF64/RIFX remain follow-up work.
The Source page identifies Software audio, duration and frame count; the status
tooltip shows input format and conversion details.

Cycle Morph is unavailable while Software audio is the source. Both A/B endpoints,
Amount and the raw host enable parameter are retained in DAW state; playback
ignores that enable parameter until returning to a cycle-capable source.
The keyboard Morph slider shows N/A. PMON still uses 12 note pairs and Sample
uses 24 voices. Noise keeps the recording while using its shared generator.
See the [WAV import record](docs/wav_import_20261101.md) and
[Software audio verification](docs/software_audio_20261101.md).

## Save a sound for the Web Synth

Choose **Save Web...** in the common header, then choose a `.spu.json` filename.
The saved sound snapshot at the time you press the button is prepared on a worker and
saved through the native file dialog. The current instrument and any pending
sound change keep their state. Cancellation or a failed write preserves an
existing destination file.

Builtin, the selected VAB Tone, a PCM cycle and imported ADPCM export as v1.
Software audio exports as v2 with the exact converted PCM16 embedded as base64.
Its masked Morph enable is exported Off, retaining A/B and Amount.
Active Morph exports A/B, Amount and the current blend as a cyclic ADPCM sample.
Inactive Morph retains the base sample. Unapplied drafts are not exported.
Web snapshots contain one instrument; they do not contain the original VAB bank
or all plugin project settings.

Plugin Gain is excluded and is not baked into voice volumes. Adjust the Web
Master for its different calibration; even a muted plugin can export a sound.
Custom ADSR and Reverb send are stored as effective voice settings. Disabled
Reverb exports Off; Noise priority clears masked PMON/Morph enable flags while
retaining their other settings. The export status tooltip describes these
format conversions. The DAW project keeps its original state.

Morph/PMON use different sample representations on Web and Native, so exact
waveforms and output levels are not guaranteed. The Web UI may canonicalize
legacy PCM encoding when it loads the authored cycle. A fresh Web app also
reserves editor sample RAM: arbitrary ADPCM exports are limited to 518656 bytes,
or 518592 with active PMON. Reload the Web app if earlier loads fill its bank.
See the [export verification record](docs/web_instrument_export_20261101.md).

The interoperability check uses the actual Web source checkout and optional
headless Chromium/WebKit, with physical audio output muted:

```sh
build/offline/web_instrument_export_test --write-fixtures build/web-export-cases
node scripts/check_web_instrument_export.mjs --web-root ../.. \
  --cases build/web-export-cases --engines chromium,webkit \
  --out build/web-export-browser-report.json
```

## Embedded DAW state

Custom state embeds the complete original VAB or authored PCM cycle, sparse Program/Tone selection,
Gain, custom ADSR/Apply, Reverb, Noise, PMON and Morph settings, including both authored A/B endpoints.
Restoring a project prepares a fresh SPU synchronously before reporting
success; it does not need an external sample path. Malformed or unsupported state
keeps the current sound, parameters and an already prepared pending replacement.
Previous Gain-only AU/VST3 state remains readable and migrates to legacy Built-in Sine.
Schema 1 VAB/Gain states also remain readable and use their original Tone ADSR.
Schema 2 keeps the original bank bytes and adds the custom ADSR settings.
Schema 3 adds the shared Reverb settings. Schema 1/2 and Gain-only states restore
with Reverb Off, preserving the original sound. Live delay history is not saved;
restoring a project starts the effect with empty buffers.
Schema 4 adds a PCM-cycle source kind with an exact 448-byte Float32 payload.
Schema 1/2/3 Builtin and VAB states remain readable with their saved settings.
Schema 5 adds the shared Noise settings after the unchanged 72-byte layout.
Schema 1/2/3/4 and Gain-only states use Noise Off / Clock 48 without changing
their source or the envelope/effect settings supported by that schema.
Schema 6 appends PMON settings, retaining all earlier layouts and reading
schema 1–5 with PMON Off. Both raw source flags are retained; simultaneous
Noise and PMON use Noise during playback.
Schema 7 appends Morph Enabled/Amount and two exact 112-sample Float32 endpoints,
while retaining the original source kind and bytes. Schema 1–6 and legacy state
restore with Morph Off, Amount 0 and the default A/B pair.
Schema 8 adds an original ADPCM sample source, including exact sample bytes and
its tuning, loop, signed volumes, raw ADSR/send, name and optional authored PCM.
The 1024-byte fixed layout and previous source payloads are retained; schemas
1–7 remain readable without requiring the original `.spu.json` file.
Schema 9 adds Software PCM kind 4: a 64-byte metadata header, little-endian
PCM16 and a bounded UTF-8 name. The 1024-byte fixed tail and all 26 parameter
IDs are unchanged. Schemas 1–8 retain their earlier kinds and small-source caps.
Source metadata edits and export snapshots share immutable PCM; actual project
save/restore creates bounded serialized data off the audio thread.
Schema 10 adds source kind 5 for full-range Builtin Sine, without a sample payload
or Program/Tone selection. All 26 parameters and the 1024-byte fixed layout remain
unchanged. Builtin kind 0 from all supported schemas retains legacy Sine playback. New saves use schema
10, which earlier plugin builds reject; keep earlier project copies for rollback.

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
  --expected docs/preset_split_vab_20261101.json --backup-root build/au-backups
# Use the printed manifest path to roll back; refuses an altered installation:
python3 scripts/install_au.py --rollback build/au-backups/au-update-REPLACE/manifest.json
```

The [preset-name and split VAB record](docs/preset_split_vab_20261101.md) contains the
[current installed manifest](docs/preset_split_vab_20261101.json), retained backup
and rollback procedure. The installed AU passed auval and the full AU host
regressions, including channel-only CC120, preserved shared Reverb tails,
PMON pair reuse, live Gain ramps and restore/reset/reactivation/bypass,
as well as the [loop compatibility fix](docs/loop_repeat_20261101.md).
A copied GarageBand project also restored its embedded VAB, Program 3, Tone 0
and Gain 99% after save, full host exit and reopen.
AU validation now passes without the preset-name warning. Host user-preset names
round-trip independently of the embedded sound; selecting factory preset 0
restores the named built-in default. The validation script treats a returning
preset-name warning as a failure even when auval exits successfully.

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

- Legacy 112-frame Sine, authored PCM and Morph cycles reach the SPU PITCH ceiling
  at about 1,575 Hz; notes 92 and above clamp. New Builtin Sine uses prepared
  multisample zones for the full MIDI/Bend range. This does not change the
  sample-dependent limits of VAB, imported sources or the PMON modulator.
- Host rate conversion uses continuous linear interpolation. Anti-alias filtering
  below 44.1 kHz and improved conversion quality remain follow-up work.
- State schema 10 stores a legacy or full-range built-in instrument, one selected VAB Tone, one
  authored 112-sample PCM cycle, one imported ADPCM sample or one Software PCM recording.
  VAB is limited to 1 MiB, ADPCM to 520192 bytes and Software PCM to 32 MiB. VAB key zones, layers and game-driver performance state are
  not reconstructed. Held notes, release tails, decoder caches and host rate
  are not saved.
- Configurable Pitch Bend range remains TODO.
- Sample-accurate ordinary parameter automation remains TODO; Gain output smoothing is sample-based after each block target is received.
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
