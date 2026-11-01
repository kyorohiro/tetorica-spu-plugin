// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuAdpcm.h"
#include "SoftwarePcm.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

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

struct WavAudioImportResult {
  std::variant<RawAdpcmSample,SoftwarePcmSample> sample;
  std::uint32_t sampleRate=0;
  unsigned channels=0;
  std::size_t sourceFrames=0;
  std::size_t resampledFrames=0;
  std::size_t paddedFrames=0; // Only ADPCM pads to 28 frames; software PCM does not.
  bool softwareAudio() const noexcept {return std::holds_alternative<SoftwarePcmSample>(sample);}
};
// Shared WAV validation/downmix/resampling. Preserve the existing ADPCM path
// while it fits physical SPU RAM; only capacity overflow selects immutable
// signed PCM16 software storage. Input and decoded PCM have separate bounds.
// Quantization follows Web Math.round (negative halves round toward +infinity).
WavAudioImportResult importWavAudio(vab::BytesView bytes,std::string name,WavImportOptions options={});
} // namespace tetorica
