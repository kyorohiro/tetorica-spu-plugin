// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuEnvelope.h"
#include "SpuWaveform.h"
#include "VabBank.h"
#include <optional>
#include <string>
#include <vector>

namespace tetorica {
inline constexpr std::size_t kMaxRawAdpcmNameBytes=320;
struct RawAdpcmSample {
  std::vector<std::uint8_t> adpcm;
  double baseFrequencyHz=44100./112;
  std::optional<std::uint32_t> loopStartSamples;
  double volumeLeft=.75,volumeRight=.75;
  EnvelopeRegisters envelope{};
  bool reverb=false;
  std::uint16_t inspectorPitch=0x1000; // Retained metadata; keyboard derives PITCH from tuning.
  std::string name;
  std::optional<PcmCycle> waveform;
};
// Pure structural validation matching Web preparePs1ADPCM. Optional authored
// PCM must match either retained Web encoder. Throws invalid_argument; no SPU.
// Native preparation retains original repeat flags and the explicit loop position.
void validateRawAdpcmSample(const RawAdpcmSample& sample);
bool sameRawAdpcmSample(const RawAdpcmSample& a,const RawAdpcmSample& b) noexcept;
// Validated owned ADPCM/tuning/envelope/send with absolute volume magnitudes.
// SpuInstrument(RawAdpcmSample) applies signed channel phases separately.
vab::PreparedTone prepareRawAdpcmSample(const RawAdpcmSample& sample);
} // namespace tetorica
