// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <cstdint>

namespace tetorica {

enum class ReverbPreset : std::uint32_t {
  Off, Room, StudioSmall, StudioMedium, StudioLarge, Hall, SpaceEcho, HalfEcho
};

struct ReverbSettings {
  ReverbPreset preset = ReverbPreset::Off;
  bool enabled = false;
  bool useToneSend = false;
  double volumeLeft = .25;
  double volumeRight = .25;
};

} // namespace tetorica
