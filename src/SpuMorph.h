// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuWaveform.h"

namespace tetorica {
struct MorphEndpoints {
  PcmCycle a=makePcmCycle(WaveformShape::Sine);
  PcmCycle b=makePcmCycle(WaveformShape::Saw);
};
struct MorphSettings {
  bool enabled=false;
  double amount=0;
};
// Pure functions; invalid endpoints or amount throw std::invalid_argument.
void validateMorphEndpoints(const MorphEndpoints& endpoints);
PcmCycle blendMorph(const MorphEndpoints& endpoints,double amount);
} // namespace tetorica
