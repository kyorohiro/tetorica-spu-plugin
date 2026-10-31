// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <array>
#include <cstdint>

namespace tetorica {

using PcmCycle = std::array<float,112>;
enum class WaveformShape { Sine,Triangle,Saw,Square };

// Original amplitude0.8 periodic shapes. No normalization, envelope or effects.
// Throws std::invalid_argument for an unknown shape.
PcmCycle makePcmCycle(WaveformShape shape);
// Throws std::invalid_argument if any point is not finite or outside[-1,1].
void validatePcmCycle(const PcmCycle& pcm);
// Pure preparation work: four28-frame ADPCM packets, END+REPEAT and loop0.
// Cyclic prediction is scored against the actual second decode pass. No SPU
// handle, RAM upload, interpolation, normalization or render work occurs here.
// Throws std::invalid_argument for invalid PCM. Call away from audio callbacks.
std::array<std::uint8_t,64> encodePcmCycle(const PcmCycle& pcm);

} // namespace tetorica
