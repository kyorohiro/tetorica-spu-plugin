// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include <array>
#include <cstdint>

namespace tetorica {
// Original 0.8 * sin(2*pi*n/112), Float32 PCM, encoded once with our BSD cyclic
// encoder (hello_ymfm/docs/js/ps1_adpcm_encoder.js). Fixed bytes are shared
// by playback and portable source extraction;
// a future encoder change must not silently alter an existing builtin sound.
// No game recording or sample bank. Raw second-pass SNR: 60.12 dB.
inline constexpr std::array<std::uint8_t,64> kBuiltinSineAdpcm{{
  38,6,68,85,85,102,117,118,102,119,119,118,103,103,87,102,
  38,0,70,70,69,52,36,35,18,17,0,224,224,222,222,220,
  38,0,188,188,171,171,170,154,154,169,153,169,153,170,169,155,
  38,3,172,187,204,204,237,221,255,14,240,2,33,34,51,67
}};
} // namespace tetorica
