// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include <array>
#include <cstdint>

namespace tetorica {
enum class BuiltinSineMode { Legacy, FullRange };
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

// FullRange is an explicit software-authored multisample instrument. Each
// immutable112-point source contains the indicated number of sine periods.
// Generated once by encodePcmCycle from Float32(0.8*sin(2*pi*n/112*factor)),
// with separate IEEE754 operations (-ffp-contract=off), as in SpuWaveform.cpp.
// Zone0 remains the original literal above, including its decoder history.
inline constexpr std::array<unsigned,5> kBuiltinSineZoneFactors{{1,2,4,8,16}};
inline constexpr std::array<std::array<std::uint8_t,64>,5> kBuiltinSineZoneAdpcm{{
  kBuiltinSineAdpcm,
  {{37,6,84,68,84,68,67,35,35,1,1,224,239,221,206,204,
    69,0,255,222,222,205,220,203,204,204,220,236,237,254,15,31,
    37,0,69,68,69,68,52,51,34,17,16,14,238,206,206,220,
    37,3,203,203,204,219,235,236,238,14,15,17,18,51,51,53}},
  {{69,6,36,34,15,237,204,188,219,220,254,16,50,83,68,69,
    69,0,36,3,0,237,204,188,219,220,254,16,50,83,68,69,
    69,0,36,3,0,237,204,188,219,220,254,16,50,83,68,69,
    69,3,36,34,15,237,204,188,219,220,254,16,50,83,68,69}},
  {{50,6,16,52,68,17,222,204,237,16,67,52,2,207,205,252,
    34,0,51,255,188,204,14,50,85,51,255,188,204,14,50,85,
    34,0,51,255,188,204,14,50,85,51,255,188,204,14,50,85,
    34,3,51,255,188,204,14,50,85,51,255,188,204,14,50,85}},
  {{0,6,80,54,173,11,101,211,186,80,54,173,11,101,211,186,
    49,0,21,174,29,84,209,219,65,36,189,13,69,210,219,80,
    49,0,21,189,29,84,209,204,65,21,174,29,84,209,204,65,
    49,3,21,174,29,84,209,219,65,36,189,13,69,210,219,80}}
}};
} // namespace tetorica
