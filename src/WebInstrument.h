// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "InstrumentState.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace tetorica {
inline constexpr std::size_t kMaxWebV1InstrumentBytes=8*1024*1024;
inline constexpr std::size_t kMaxWebInstrumentBytes=48*1024*1024;
struct ImportedWebInstrument {
  std::string name;
  SavedInstrumentState state;
};
// Non-RT pure import. Strict bounded Web .spu.json versions1/2; no file I/O,
// native handles or publication. Throws on the complete invalid input tuple.
ImportedWebInstrument readWebInstrument(std::string_view text);
} // namespace tetorica
