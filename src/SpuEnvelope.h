// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <cstdint>

namespace tetorica {

// Raw PS1 register values. Rate fields are table indexes, not milliseconds.
struct EnvelopeRegisters {
  std::uint16_t adsr1 = 0x000f;
  std::uint16_t adsr2 = 0x1fc0;
};

// Authored plugin override. The original bank/Tone ADSR remains independent.
struct EnvelopeSettings {
  EnvelopeRegisters custom{};
  bool enabled = false;
  bool live = true;
};

} // namespace tetorica
