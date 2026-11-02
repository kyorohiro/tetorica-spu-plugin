# Tetorica SPU Plugin v0.0.1 Preview — release notes draft

Candidate preparation only. This file does not announce a published release.

## Changes since v0.0.0

- Add Octave − / + controls and a momentary ±2-semitone Pitch wheel beside Morph in the editor keyboard. These controls affect editor performance; DAW MIDI note numbers remain unchanged.
- Extend Gain to 380%, with orange feedback above 100%. Existing Gain automation retains its original mapping; an additional Boost parameter supplies the extra range.
- Add Auto apply, enabled by default, for PCM shapes and completed drawing strokes, including Morph endpoints. Turn it off to edit a draft before applying it.
- Process Gain, Boost, Morph Amount and both Reverb returns on a bounded sample-offset automation timeline, retaining smoothing.
- Distinguish sound preparation from a prepared sound waiting for host audio processing. When Ready, start DAW playback to let the host adopt the sound; Standalone users are directed to audio settings.

GarageBand checks cover hand-drawn Morph and left/right Reverb automation playback, plus project save, quit and reopen. Real-time gesture recording has not been verified.

## Candidate formats

- macOS 13+: Universal Standalone APP, AUv2 and VST3 in a Developer ID signed, notarized DMG. Signing and notarization must pass for the actual candidate before publication.
- Windows x64: unsigned VST3 ZIP, built and checked on Windows CI. Uses the DAW's audio driver.
- Linux DAW plugins and Windows Standalone are not included.

Each candidate includes installation instructions, license notices, a source/file manifest and a SHA256 sidecar. No game sample banks are bundled.

## Project compatibility

Version 0.0.1 reads the older embedded instrument states. New saves use schema 11 to retain Gain and Boost separately; v0.0.0 cannot read these new saves. Keep a copy of projects before saving them with the updated plugin. Plugin identifiers and the original Gain parameter mapping are retained.

## Remaining checks

- Three GarageBand exports differed by up to 1–3 PCM16 LSB near the beginning. Two comparisons failed the existing 1 LSB limit and one passed. The cause remains open; no bit-identical export claim is made.
- Real-time automation gesture recording, physical listening, latency and long-running DAW sessions remain unverified.
- Windows checks use CI hosts; general Windows DAW GUI/file panels and macOS 13 hardware have not been verified.

See [GarageBand verification](garageband_automation_20261102.md), [TODO](issues/TODO.md) and the [v0.0.1 candidate record](preview_v0_0_1_20261102.md).
