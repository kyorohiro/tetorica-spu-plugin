// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuWaveform.h"

namespace tetorica {

// Native SPU adjacent-voice pitch modulation, using a software-prepared
// periodic modulator outside physical sample RAM. Pairing permits12 notes.
struct PmonSettings {
  bool enabled = false;
  WaveformShape waveform = WaveformShape::Sine;
  double ratio = 1;
  double depth = .5;
  double decayMs = 500;
};

} // namespace tetorica
