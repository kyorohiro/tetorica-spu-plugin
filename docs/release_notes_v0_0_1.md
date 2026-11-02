# Tetorica SPU Plugin v0.0.1 Preview

A developer preview of a native PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native. The audio engine runs as C++ inside the host callback. No game sample banks are bundled.

## Changes since v0.0.0

- Add Octave − / + controls and a momentary ±2-semitone Pitch wheel beside Morph in the editor keyboard. These controls affect editor performance; DAW MIDI note numbers remain unchanged.
- Extend Gain to 380%, with orange feedback above 100%. Existing Gain automation retains its original mapping; an additional Boost parameter supplies the extra range.
- Add Auto apply, enabled by default, for PCM shapes and completed drawing strokes, including Morph endpoints. Turn it off to edit a draft before applying it.
- Process Gain, Boost, Morph Amount and both Reverb returns on a bounded sample-offset automation timeline, retaining existing Gain smoothing.
- Distinguish sound preparation from a prepared sound waiting for host audio processing. When Ready, start DAW playback to let the host adopt the sound; Standalone users are directed to audio settings.

GarageBand checks cover hand-drawn Morph and left/right Reverb automation playback, plus project save, quit and reopen. Real-time gesture recording has not been verified.

## Downloads

- macOS 13+: Universal Standalone APP, AUv2 and VST3 in a Developer ID signed, notarized DMG, verified on Apple Silicon and Intel CI hosts.
- Windows x64: unsigned VST3 ZIP, built and checked on Windows CI. Uses the DAW's audio driver.
- Linux DAW plugins and Windows Standalone are not included.

Each package includes installation instructions, license notices, a source/file manifest and a SHA256 sidecar. No game sample banks are bundled.

## Installation

Close audio hosts before changing plugin bundles and retain previous copies. Keep bundle directories intact.

- macOS: open the DMG and its versioned folder. Use `TetoricaSPU.app` for Standalone, copy `TetoricaSPU.component` to `~/Library/Audio/Plug-Ins/Components/`, or copy `TetoricaSPU.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
- Windows: copy the entire `TetoricaSPU.vst3` directory to `C:\Program Files\Common Files\VST3\` or your DAW's configured VST3 folder, then restart/rescan the DAW.

See the included `INSTALL.txt` for details and keep the original notarized DMG.

## Project compatibility

Version 0.0.1 reads the older embedded instrument states. New saves use schema 11 to retain Gain and Boost separately; v0.0.0 cannot read these new saves. Keep a copy of projects before saving them with the updated plugin. Plugin identifiers and the original Gain parameter mapping are retained.

## Verification

All eight CI jobs passed on source [`b80a8a01fd278bda4a1463bdebb6cd8b1fc49422`](https://github.com/kyorohiro/tetorica-spu-plugin/tree/b80a8a01fd278bda4a1463bdebb6cd8b1fc49422): [core/native checks](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41432227747), [Mac/Windows packaging](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41432227796), and [signing/notarization plus Apple Silicon/Intel verification](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41432505698). Both Mac architectures checked the same final DMG. Apple's notarization result was Accepted, with no issues.

## Remaining checks

- Three GarageBand exports differed by up to 1–3 PCM16 LSB near the beginning. Two comparisons failed the existing 1 LSB limit and one passed. The cause remains open; no bit-identical export claim is made.
- Real-time automation gesture recording, physical listening, latency and long-running DAW sessions remain unverified.
- Windows checks use CI hosts; general Windows DAW GUI/file panels and macOS 13 hardware have not been verified.

See [GarageBand verification](https://github.com/kyorohiro/tetorica-spu-plugin/blob/772bf41669a0619021247b6b6e7210de0cf04eff/docs/garageband_automation_20261102.md), [TODO](https://github.com/kyorohiro/tetorica-spu-plugin/blob/772bf41669a0619021247b6b6e7210de0cf04eff/docs/issues/TODO.md) and the [v0.0.1 candidate record](https://github.com/kyorohiro/tetorica-spu-plugin/blob/772bf41669a0619021247b6b6e7210de0cf04eff/docs/preview_v0_0_1_20261102.md).
