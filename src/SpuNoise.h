// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <cstdint>

namespace tetorica {

// One shared native Noise generator; clock is a raw6-bit rate/texture code,
// not a musical pitch. MIDI keys still control independent voice envelopes.
struct NoiseSettings {
  bool enabled = false;
  std::uint32_t clock = 48;
};

} // namespace tetorica
