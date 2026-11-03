# Tetorica SPU Plugin v0.0.2 Preview

A native PS1 SPU instrument for DAWs, using iPlug2 and tetorica-soundchip-native. This preview adds PSF1 / MiniPSF sound extraction. No game sample banks are bundled.

## Changes since v0.0.1

- Open a folder containing PSF1 / MiniPSF files and their PSFLib dependencies, preserving subfolder paths. Select an Entry, an extracted VAB Bank, and its Program / Tone.
- Load a PSF file directly through **Load sound...**. For libraries outside that file's parent folder, use **PSF... → Open PSF folder...** and choose a folder containing the required set.
- Automatically select the first supported bank and playable Tone. Cancelled, missing-dependency and invalid-file requests retain the previous sound.
- Embed the selected VAB in the DAW project so its Program / Tone can be restored without the original PSF set. The Entry / Bank catalog is not saved; reopen the folder to browse other sounds.

This extracts supported static VAB sounds. It does not play the original song or execute the PS1 CPU / game driver. PSF2, banks generated at runtime, and ambiguous separated VH / VB data are unsupported.

## Using PSF sounds

Choose **PSF... → Open PSF folder...**, then use **Entry**, **Bank**, **Program** and **Tone**. Keep the folder to the needed files: limits include 512 PSF-related files, 64 MiB total, 4 MiB per file and 8192 directory entries.

PSF loading retains your existing Gain / ADSR / Reverb / Noise / PMON / Morph settings. Use Sample source and Morph Off to inspect the original Tone. If the plugin displays Ready, start DAW playback so the host processes and adopts the prepared sound.

## Downloads and installation

- macOS 13+: Universal Standalone APP, AUv2 and VST3 in a Developer ID signed, notarized DMG. Keep the original DMG and its stapled ticket.
- Windows x64: unsigned VST3 ZIP using the DAW's audio driver.
- Linux plugins and Windows Standalone are not included.

Close audio hosts before replacing plugin bundles and keep project backups. Copy whole bundle directories using the included `INSTALL.txt`:

- macOS AU: `~/Library/Audio/Plug-Ins/Components/`
- macOS VST3: `~/Library/Audio/Plug-Ins/VST3/`
- Windows VST3: `C:\Program Files\Common Files\VST3\` or the DAW's configured VST3 folder.

Each package includes license notices, a source/file manifest and a SHA256 sidecar. Plugin IDs, the 27 parameters and state schema 11 are unchanged from v0.0.1. Older states remain readable; v0.0.0 cannot read schema 11 saves.

## Verification and remaining checks

All eight CI jobs passed on source [`626dcecfe50ab80c6e97cce803478d0774a3241a`](https://github.com/kyorohiro/tetorica-spu-plugin/tree/626dcecfe50ab80c6e97cce803478d0774a3241a): [core/native checks](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41546527681), [Mac/Windows packaging](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41546527632), and [signing/notarization plus Apple Silicon/Intel verification](https://github.com/kyorohiro/tetorica-spu-plugin/actions/runs/41546578256). Both Mac architectures checked the same final DMG. Apple's notarization result was Accepted, with no issues. The downloaded Mac and Windows candidates also passed local package verification.

GarageBand 10.4.12 checks with original demo fixtures cover folder / Entry / Bank / Program / Tone selection, cancel / CRC failure preservation, and full quit / reopen without the PSF source files. The selected VAB and complete saved payload matched before and after restoration. These GUI checks used the PSF development build before the v0.0.2 version metadata change.

Real-game PSF sets, physical listening and long-running DAW sessions remain unverified and are planned separately. Real-time automation gesture recording, general Windows DAW GUI/file panels and macOS 13 hardware also remain unverified. The previously recorded 1–3 PCM16 LSB differences near the beginning of some GarageBand exports are unresolved; no bit-identical DAW export claim is made.

See the [candidate verification record](https://github.com/kyorohiro/tetorica-spu-plugin/blob/de8a40c749e41f764ef08246eb7547e15168390f/docs/preview_v0_0_2_20261103.md), [GarageBand PSF checks](https://github.com/kyorohiro/tetorica-spu-plugin/blob/de8a40c749e41f764ef08246eb7547e15168390f/docs/au_psf_update_20261103.md) and [TODO](https://github.com/kyorohiro/tetorica-spu-plugin/blob/de8a40c749e41f764ef08246eb7547e15168390f/docs/issues/TODO.md).
