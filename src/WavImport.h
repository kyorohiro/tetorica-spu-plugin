// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuAdpcm.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace tetorica {
inline constexpr std::size_t kMaxWavFileBytes=32u*1024u*1024u;
struct WavImportOptions {int rootMidiNote=60;bool loop=false;};
struct WavImportResult {
  RawAdpcmSample sample;
  std::uint32_t sampleRate=0;
  unsigned channels=0;
  std::size_t sourceFrames=0;
  std::size_t resampledFrames=0;
  std::size_t paddedFrames=0; // Added zeros, 0..27; loop length includes them.
};
// Non-RT pure conversion: bounded RIFF/WAVE PCM16 or IEEE Float32, mono or
// stereo, averaged to mono and band-limited to 44100 Hz. No normalization.
// Packet padding is explicit; oversized samples are rejected, never trimmed.
// Only complete-file RIFF is accepted, not RF64/RIFX/extensible/compressed WAV.
// Throws invalid_argument on malformed/unsupported input; allocates no native
// SPU handle and never mutates an existing instrument or the input bytes.
WavImportResult importWav(vab::BytesView bytes,std::string name,WavImportOptions options={});
} // namespace tetorica
