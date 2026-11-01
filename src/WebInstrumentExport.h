// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "InstrumentState.h"
#include <string>
#include <string_view>
#include <vector>

namespace tetorica {
struct ExportedWebInstrument {
  std::string json;
  std::vector<std::string> notes;
};
// Non-RT bounded Web v1 ADPCM / v2 Software PCM snapshot. Gain is intentionally not baked into
// portable voice volumes. Notes describe projection and playback differences;
// this does not mutate the source, create a native handle, or write any file.
ExportedWebInstrument writeWebInstrument(const SavedInstrumentState& state,std::string_view name);
} // namespace tetorica
