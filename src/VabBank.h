// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// C++ port of Tetorica's playground_ps1_vab.js / preparePs1ADPCM.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tetorica::vab {

inline constexpr std::size_t kMaxFileBytes = 1024 * 1024;
inline constexpr std::size_t kMaxBodyBytes = 512 * 1024;
inline constexpr std::size_t kMaxSampleBytes = kMaxBodyBytes - 0x1000;

struct BytesView {
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
};

struct Tone {
  std::uint16_t program = 0, index = 0, sample = 0;
  std::uint8_t mode = 0, volume = 0, pan = 0, center = 0, shift = 0;
  std::uint8_t keyLow = 0, keyHigh = 0;
  std::uint16_t adsr1 = 0, adsr2 = 0;
  std::uint8_t vibratoWidth = 0, vibratoTime = 0;
  std::uint8_t portamentoWidth = 0, portamentoTime = 0;
  std::uint8_t bendMin = 0, bendMax = 0;
};

struct Program {
  std::uint16_t index = 0;
  std::uint8_t volume = 0, pan = 0;
  // Packed positions in Bank::tones, avoiding two independently mutable copies
  // of each Tone's metadata. Program IDs themselves may be sparse.
  std::vector<std::size_t> toneIndices;
};

struct Sample {
  std::uint16_t index = 0; // One-based Sony Sample number.
  std::vector<std::uint8_t> bytes;
};

struct Bank {
  std::uint32_t version = 0, id = 0;
  std::uint8_t masterVolume = 0, masterPan = 0;
  std::vector<Program> programs;
  std::vector<Tone> tones;
  std::vector<Sample> samples;
};

struct PreparedTone {
  std::vector<std::uint8_t> adpcm;
  double rootFrequencyHz = 0;
  std::uint32_t decodedFrames = 0;
  std::optional<std::uint32_t> loopStartSamples;
  std::uint16_t adsr1 = 0, adsr2 = 0;
  double volumeLeft = 0, volumeRight = 0;
  bool reverb = false;
  std::size_t trimmedBytes = 0;
  Tone tone;
};

// Standard Sony VAB v6/v7 only. All retained metadata/sample bytes are owned.
// Malformed/truncated/oversized input throws std::runtime_error. A null pointer
// with nonzero length throws std::invalid_argument. No SPU handle is touched.
Bank parse(BytesView complete);
Bank parse(BytesView header, BytesView body);

// Bank metadata is public for tools/editors: validate it again defensively here.
// Preserve the selected ADPCM byte-for-byte through its own first END packet.
PreparedTone prepareTone(const Bank& bank, std::size_t flatIndex);

} // namespace tetorica::vab
