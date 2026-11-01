// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuAdpcm.h"

namespace tetorica {
class InstrumentDefinition;
// Pure immutable source extraction. No native handle, audio state, Gain,
// envelope override or Morph application. The caller resolves authored edits.
// Fixed builtin bytes, canonical PCM, selected VAB Tone or exact raw source.
RawAdpcmSample extractAdpcmSource(const InstrumentDefinition& definition);
} // namespace tetorica
