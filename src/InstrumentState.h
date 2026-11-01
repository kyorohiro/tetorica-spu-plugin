// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "VabBank.h"
#include "SpuEnvelope.h"
#include "SpuReverb.h"
#include "SpuWaveform.h"
#include "SpuNoise.h"
#include "SpuPmon.h"
#include "SpuMorph.h"
#include "SpuAdpcm.h"
#include "SoftwarePcm.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace tetorica {

inline constexpr std::size_t kStateHeaderBytes=32;
inline constexpr std::array<std::uint8_t,8> kStateMagic{{'T','S','P','U','S','T','0','1'}};
inline constexpr std::uint32_t kStateSchemaVersion=9;
inline constexpr std::size_t kStateFixedBytes=1024;
inline constexpr std::size_t kMaxStateBytes=kMaxSoftwarePcmBytes+64+kMaxSoftwarePcmNameBytes+kStateFixedBytes;

// Factory-created immutable authoring data, independent of mutable audio state.
// Full bank bytes are owned so saved projects never require the original path.
class InstrumentDefinition final {
public:
  enum class Kind : std::uint32_t { Builtin=0,Vab=1,PcmCycle=2,AdpcmSample=3,SoftwarePcm=4 };
  static std::shared_ptr<const InstrumentDefinition> builtin();
  static std::shared_ptr<const InstrumentDefinition> fromVab(
      std::vector<std::uint8_t> bytes,std::uint32_t sparseProgram,std::uint32_t toneIndex);
  static std::shared_ptr<const InstrumentDefinition> fromVabFlatIndex(
      std::vector<std::uint8_t> bytes,std::size_t flatIndex);
  static std::shared_ptr<const InstrumentDefinition> fromPcmCycle(const PcmCycle& cycle);
  static std::shared_ptr<const InstrumentDefinition> fromAdpcmSample(const RawAdpcmSample& sample);
  static std::shared_ptr<const InstrumentDefinition> fromSoftwarePcmSample(const SoftwarePcmSample& sample);
  static std::shared_ptr<const InstrumentDefinition> withMorphEndpoints(
      const std::shared_ptr<const InstrumentDefinition>& source,const MorphEndpoints& endpoints);

  InstrumentDefinition(const InstrumentDefinition&)=delete;
  InstrumentDefinition& operator=(const InstrumentDefinition&)=delete;
  InstrumentDefinition(InstrumentDefinition&&)=delete;
  InstrumentDefinition& operator=(InstrumentDefinition&&)=delete;

  Kind kind() const noexcept { return mKind; }
  const std::vector<std::uint8_t>& bankBytes() const noexcept { return mBank; }
  std::uint32_t program() const noexcept { return mProgram; }
  std::uint32_t tone() const noexcept { return mTone; }
  // Valid authoring data for Kind::PcmCycle; zero-initialized for other kinds.
  const PcmCycle& pcmCycle() const noexcept { return mPcmCycle; }
  const MorphEndpoints& morphEndpoints() const noexcept { return mMorphEndpoints; }
  const RawAdpcmSample& rawAdpcmSample() const noexcept { return mRawAdpcm; }
  const SoftwarePcmSample& softwarePcmSample() const noexcept { return mSoftwarePcm; }

private:
  InstrumentDefinition(Kind kind,std::vector<std::uint8_t> bank,
                       std::uint32_t program,std::uint32_t tone,PcmCycle cycle={},MorphEndpoints endpoints={},RawAdpcmSample raw={},SoftwarePcmSample software={});
  const Kind mKind;
  const std::vector<std::uint8_t> mBank;
  const std::uint32_t mProgram,mTone;
  const PcmCycle mPcmCycle;
  const MorphEndpoints mMorphEndpoints;
  const RawAdpcmSample mRawAdpcm;
  const SoftwarePcmSample mSoftwarePcm;
};

struct SavedInstrumentState {
  std::shared_ptr<const InstrumentDefinition> definition;
  double gainPercent=100;
  EnvelopeSettings envelope{};
  ReverbSettings reverb{};
  NoiseSettings noise{};
  PmonSettings pmon{};
  MorphSettings morph{};
};

// Validates the complete scalar tuple and immutable source without allocating
// an encoded state. Software PCM validation is O(1) in recording length.
void validateSavedInstrumentState(const SavedInstrumentState& state);

// Pure bounded codec: no native SDK, filesystem, audio callbacks or handles.
// Throws on malformed/oversized/unsupported state; requires an exact total size.
std::vector<std::uint8_t> encodeState(const InstrumentDefinition& definition,double gainPercent,
                                    EnvelopeSettings envelope={},ReverbSettings reverb={},NoiseSettings noise={},PmonSettings pmon={},MorphSettings morph={});
std::vector<std::uint8_t> encodeState(
    const std::shared_ptr<const InstrumentDefinition>& definition,double gainPercent,
    EnvelopeSettings envelope={},ReverbSettings reverb={},NoiseSettings noise={},PmonSettings pmon={},MorphSettings morph={});
SavedInstrumentState decodeState(vab::BytesView bytes);

class SpuInstrument;
// Non-RT synchronous preparation, implemented with the exchange/native layer,
// deliberately not in InstrumentState.cpp. Native preparation runs here after
// the pure codec's structural/selected ADPCM validation succeeds.
std::unique_ptr<SpuInstrument> prepareDefinition(const InstrumentDefinition& definition);

} // namespace tetorica
