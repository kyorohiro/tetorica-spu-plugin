// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "VabBank.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tetorica {

inline constexpr std::size_t kStateHeaderBytes=32;
inline constexpr std::array<std::uint8_t,8> kStateMagic{{'T','S','P','U','S','T','0','1'}};
inline constexpr std::uint32_t kStateSchemaVersion=1;
inline constexpr std::size_t kMaxStateBytes=vab::kMaxFileBytes+40;

// Factory-created immutable authoring data, independent of mutable audio state.
// Full bank bytes are owned so saved projects never require the original path.
class InstrumentDefinition final {
public:
  enum class Kind : std::uint32_t { Builtin=0,Vab=1 };
  static std::shared_ptr<const InstrumentDefinition> builtin();
  static std::shared_ptr<const InstrumentDefinition> fromVab(
      std::vector<std::uint8_t> bytes,std::uint32_t sparseProgram,std::uint32_t toneIndex);
  static std::shared_ptr<const InstrumentDefinition> fromVabFlatIndex(
      std::vector<std::uint8_t> bytes,std::size_t flatIndex);

  InstrumentDefinition(const InstrumentDefinition&)=delete;
  InstrumentDefinition& operator=(const InstrumentDefinition&)=delete;
  InstrumentDefinition(InstrumentDefinition&&)=delete;
  InstrumentDefinition& operator=(InstrumentDefinition&&)=delete;

  Kind kind() const noexcept { return mKind; }
  const std::vector<std::uint8_t>& bankBytes() const noexcept { return mBank; }
  std::uint32_t program() const noexcept { return mProgram; }
  std::uint32_t tone() const noexcept { return mTone; }

private:
  InstrumentDefinition(Kind kind,std::vector<std::uint8_t> bank,
                       std::uint32_t program,std::uint32_t tone);
  const Kind mKind;
  const std::vector<std::uint8_t> mBank;
  const std::uint32_t mProgram,mTone;
};

struct SavedInstrumentState {
  std::shared_ptr<const InstrumentDefinition> definition;
  double gainPercent=100;
};

// Pure bounded codec: no native SDK, filesystem, audio callbacks or handles.
// Throws on malformed/oversized/unsupported state; requires an exact total size.
std::vector<std::uint8_t> encodeState(const InstrumentDefinition& definition,double gainPercent);
std::vector<std::uint8_t> encodeState(
    const std::shared_ptr<const InstrumentDefinition>& definition,double gainPercent);
SavedInstrumentState decodeState(vab::BytesView bytes);

class SpuInstrument;
// Non-RT synchronous preparation, implemented with the exchange/native layer,
// deliberately not in InstrumentState.cpp. Native unsupported-loop checks run
// here after the pure codec's structural/selected ADPCM validation succeeds.
std::unique_ptr<SpuInstrument> prepareDefinition(const InstrumentDefinition& definition);

} // namespace tetorica
