# VAB import plan — 2026-10-31

This records the Phase 3 design. The bounded C++ parser, selected-Tone preparation
and fresh offline instrument are implemented in `src/VabBank.*` and
`src/SpuInstrument.*`; `spu_vab_render` exercises them without a DAW. The plugin
UI still plays its original built-in sine bank. DSP handoff, state and file UI
remain subsequent steps, not a claim of VAB support in the current plugin UI.

## Bounded parser milestone

The first implementation adds a UI-independent C++ parser and a selected
Tone preparation function, with no live SPU upload or file dialog yet. Match the
existing JavaScript implementation against the original synthetic fixture:

1. Parse complete VAB and explicit VH + VB inputs to identical bank metadata.
2. Preserve sparse Program IDs 3 and 11 and the three Tone identities.
3. Keep the sample-size table's leading reservation and bounded sample slices.
4. Prepare one Tone without decoding/re-encoding its original ADPCM.
5. Compare ADSR, tuning, loop, trimmed bytes, volume/pan and reverb-send metadata.
6. Reject truncation, size/count mismatches, invalid metadata, malformed selected
   samples and incorrect VH/VB combinations without modifying the current sound.

Only after that passes should `SpuInstrument` accept a prepared Tone definition.
Subsequent milestones are offline rendering, safe prepared-engine handoff, state
round-trip in the actual AU/VST3, then file selection and Program/Tone UI.

## Existing local references

The following paths are relative to the **parent `hello_ymfm` workspace**, not to
this independent plugin repository. They are references for a deliberate port;
the plugin build should not depend on those sibling application files.

| Purpose | Parent-workspace path |
| --- | --- |
| Shared parser and selected Tone preparation | `docs/playground/playground_ps1_vab.js` |
| SPU Synth re-export of the shared parser | `docs/synth-spu/spu_synth_vab.js` |
| VAB regression tests | `docs/synth-spu/spu_synth_vab.test.mjs` |
| ADPCM validation, tuning and physical-RAM allocation | `docs/js/ps1synth.js` (`preparePs1ADPCM`, `Ps1Synth.loadADPCM`) |
| Existing instrument/preset save format | `docs/synth-spu/spu_synth_instrument.js` |
| Single-Tone loading and previous-instrument preservation | `docs/synth-spu/synth.js` |
| Original VAB fixture generator | `test/helpers/spu_vab_fixture.mjs` |
| Reproducible demo builder | `scripts/build_spu_import_demos.mjs` |
| Original complete/split banks and license | `docs/playground/samples/spu-import/` |

The JavaScript test command, run from the parent workspace, was checked during
this investigation: `node --test docs/synth-spu/spu_synth_vab.test.mjs` — **8 tests
passed**. This does not validate a future C++ port.

The original fixtures contain no game code, game samples or external recordings.
Retain the fixture/generator BSD-3-Clause notices and license when copying them.

| Fixture | Bytes | SHA-256 |
| --- | ---: | --- |
| `spu-synth-demo.vab` | 3744 | `71a7de205c0ebe1b8743288d90cd3b0204da9bd81aed9159af31f404d7cf2be6` |
| `demo.vh` | 3616 | `8d73bed949ac8c3243a8503bd3be3491a7f9b3ace5e1f38d5a2b3e288fb8510a` |
| `demo.vb` | 128 | `ddd89a0c5270c06e1f77d70b4eb07ea649926c21e15da71a55a8e47a3fccafd8` |

The fixture has two Programs, three Tones and two Samples. Sample-size-table
entry zero reserves 16 leading VB bytes. Sample slices are 80 and 32 bytes;
selected Tone preparation trims them at their own first END to 64 and 16 bytes.

## Initial format and validation scope

Support standard Sony VAB **versions 6 and 7**, either as one complete file or
as an explicitly supplied matching VH header plus VB body. Do not infer a pair
from unrelated files, scan a VB without its header, or treat a VAG file as VAB.
PSF extraction, custom/runtime-packed banks, older VAB versions, song playback,
key-zone/layer reconstruction and game-driver effects remain later work.

Keep two result types: a structurally parsed bank and a strictly prepared
selected Tone. A bank may contain an unavailable Tone without blocking its
other usable Tones. Sample 0 is unassigned; Sample 255 is a libsnd Noise sentinel
and is unsupported by the initial importer. Preserve those entries in metadata
and explain why selection cannot be prepared.

Port the existing bounded layout deliberately:

- Require `pBAV` and little-endian fields; cap each input at 1 MiB and the VB body
  at 512 KiB before copying or allocating payload storage.
- Program/Tone counts are actual counts. Require 1–128 Programs, a nonzero Tone
  count no larger than `programCount * 16`, and at most 255 Samples.
- The 128-entry Program table starts at `0x20`. Program IDs may have holes.
  Each nonempty Program consumes one packed `0x200`-byte Tone table starting at
  `0x820`; each Tone is 32 bytes. Do not index those tables by sparse Program ID.
- The sample-size table is at `0x820 + programCount * 0x200`, with a `0x200`-byte
  table before the body. Its entry zero is a leading body reservation, not
  Sample 1. Entries are byte lengths in units of eight.
- Check every range and addition before access. Require the declared combined
  length to match header plus body; split VH must end exactly at the body offset.
  Validate counts against the tables, MIDI metadata in 0–127, key-low <= key-high,
  and sample references. Allow bounded body padding after the final Sample.
- Own the parsed data or otherwise guarantee its immutable lifetime; do not keep
  spans into a temporary file-dialog buffer.

Prepare the selected Sample within its size-table slice only. Find its first
complete END packet and trim only subsequent padding; never borrow an END from
the next Sample. Validate the retained 16-byte packets: predictor 0–4, shift
0–12, supported flags 0–7, END only on the retained final packet and coherent
loop metadata. Reject multiple loop-start markers and the initial unsupported
one-shot-with-loop-start pattern, following the current JavaScript behavior.
For a looping Sample without an explicit marker, use sample start as its repeat
address. Preserve original bytes; do not repair flags silently.

Porting these checks provides parity with the existing importer, not proof that
all standard VAB variations or real game drivers are covered.

## Tuning, voice metadata and RAM

Preserve raw `ADSR1`/`ADSR2` and repeat position. Existing tuning is:

```text
rootHz = 440 * 2^((center - shift/128 - 69) / 12)
PITCH  = round(4096 * playedNoteHz / rootHz)
```

Positive fine shift raises playback pitch, so it lowers the effective rootHz.
Report or explicitly handle the 14-bit hardware PITCH range rather than implying
all transpositions are exact. Selected-Tone audition initially plays that Tone
across the keyboard; stored key zones do not yet imply automatic layering.

The existing volume/pan model is approximate: multiply bank/Program/Tone volume
fractions, multiply their piecewise pan factors per side, then square each
result. Tone mode bit `4` requests reverb send; it does not embed or select a
complete shared reverb configuration. Keep plugin headroom and MIDI velocity
handling distinct from this imported static metadata.

SPU physical RAM is 512 KiB. Its first `0x1000` bytes are capture buffers, so
samples uploaded from `0x1000` have at most **520192 bytes** available. Enforce
16-byte packet alignment; address registers use eight-byte units. Initially
load only the selected Tone into a fresh prepared SPU instance, instead of
accumulating every previous selection until RAM is exhausted. A parsed VB may
fit the format's 512 KiB bound while a selected payload does not fit the usable
instrument RAM: these are separate validation stages.

## SDK API and cache preparation

This repository pins the Native SDK at
`cf1747387eb2b66323124959d7b356b936197784`. Its public header is
`vendor/tetorica-soundchip-native/native/include/tetorica/ps1_spu.h`; the actual
C ABI header and core are in the SDK's `vendor/mame-ps1-spu/` directory.
Use `ps1_spu_create/destroy`, `ps1_spu_load_ram`, `ps1_spu_write`,
`ps1_spu_audition_key`, and `ps1_spu_render`. Each handle is independently owned
and must not be accessed concurrently. Native rendering is always 44.1 kHz;
the plugin's continuous host-rate adapter remains outside this core.

SDK inspection found lazy `new` allocations in `get_sample_cache()` and
`get_loop_cache()`, and replacement allocation in reset. RAM upload invalidates
decoded caches. Consequently, calling NoteOn after an upload does not by itself
establish an allocation-free rendering path.

The current built-in sine constructor primes a short 112-frame Sample. A
general loader cannot reuse its fixed 1024-frame warm-up unchanged: the SDK
reaches `update_voice_loop()` at the Sample's first END, possibly much later.
Proposed preparation on a worker/state-loading thread:

1. Construct a fresh handle and upload only validated selected payload bytes.
2. Configure start/repeat addresses and temporary fast-attack, held-sustain ADSR,
   zero volume and high PITCH (`0x3fff`), with Noise/PMON disabled for preparation.
3. Render through the first END with a guard interval, creating the relevant
   decoded/loop caches. A safe bound derives from retained packet count times
   28 source frames and the preparation Pitch. Maximum usable payload is about
   910336 source frames, at most 227598 native frames at maximum Pitch, plus guard.
4. Restore authored ADSR/tuning/settings and silence/reset voice bookkeeping
   without SDK reset or RAM upload, which would invalidate the prepared caches.
5. Probe new/delete during subsequent real playback, including long samples,
   nonzero loop starts, all voices, retriggers and supported Pitch extremes.

Do not infer hard real-time certification from a C++ new/delete probe; it is a
specific regression check for the exercised immutable-bank path.

### Confirmed pinned-SDK loop limitation

The Native PCM probe confirmed an edge case: the existing JavaScript
validator accepts a loop-start marker with flag `4`, while the pinned SDK's
sample-cache discovery checks start+repeat bits (`6`) and a repeat-address
fallback checks the repeat bit. The existing nonzero-loop JavaScript test checks
metadata and does not establish actual playback for every such flag pattern.

For otherwise identical packets with a nonzero loop start, flag `4` stopped
producing audio after END; flag `6` continued. The importer still parses both
patterns, preserving parity and original bytes. Native instrument preparation
rejects looping samples whose repeat-address packet lacks repeat bit `2`,
including markerless fallback loops that point to a flag-0 first packet.
The SDK is pinned and unchanged. A later SDK fix should be independently tested
before widening this supported subset.

## Prepared-engine handoff

File selection and parsing should not mutate the current audio engine. Prepare
a complete replacement on a worker, with a request generation so an older slow
load cannot override a newer request. A failure retains the current instrument.

Publish a prepared engine pointer atomically and adopt it at an audio-block
boundary. Retire the previous engine into a bounded SPSC queue for worker-side
destruction. Do not let an audio-thread shared_ptr decrement destroy a handle or
sample storage. If the retire queue has no room, defer adoption rather than
freeing on the callback. Superseded engines that were never adopted may be
destroyed by the preparing worker after ownership is resolved atomically.

Initially, make a Tone change clear held-note bookkeeping and queued MIDI at the
commit boundary. Define the transition and any short fade explicitly; held-note
retrigger, tail preservation and crossfade are separate future features. If
the host changed sample rate during preparation, the audio thread may call the
already allocation-free `prepare(currentHostRate)` before adoption; it must not
repeat the SDK upload/reset/warm-up there.

## DAW state persistence

Use a versioned, bounded custom state chunk and set `PLUG_DOES_STATE_CHUNKS=1`.
Reference the pinned iPlug2 example
`vendor/iPlug2OOS/iPlug2/Examples/IPlugChunks/IPlugChunks.cpp` and
`IPlug/IPlugPluginBase.h`: override `SerializeState`/`UnserializeState`, including
the normal `SerializeParams`/`UnserializeParams` portion as required by iPlug2.

Proposed payload: a format/version marker, bounded embedded complete bank (or
canonical VH+VB), selected sparse Program ID and Tone index, relevant authored
instrument settings, and normal parameters such as Gain. Keeping the bank
embedded allows Program/Tone selection after a project moves to another machine.
External path/name and a source hash may be optional provenance, never required
to recover the sound. Do not save held keys, native decoder caches/capture RAM,
playing envelopes/tails, or host-controlled sample rate.

Validate every length, count, enum and selection before allocating a large
payload or publishing state. Treat malformed state as an atomic failure that
keeps the current sound and parameter settings. Keep an immutable authoring
definition for serialization rather than racing against mutable audio handles;
the ownership/lifetime design needs explicit tests.

Asynchronous file loading and DAW state restore have different completion
requirements. `UnserializeState` must not return success with only a worker
request pending: an immediate offline export could otherwise render unprepared
first blocks. The initial design should complete bounded parsing/preparation in
the host's state-loading call, outside `ProcessBlock`, and publish a ready engine
before returning. Verify AU/VST3 lifecycle and locking before implementing this;
do not assume every host calls state restore on a convenient UI thread. The
render callback still adopts a prepared engine nonblockingly.

Required integration checks include restore followed immediately by the first
render, equivalent PCM in a separate instance, missing original source files,
malformed/oversized chunks, loading while playing, overlapping load generations,
sample-rate changes during load, and destruction/queue overflow ownership.
