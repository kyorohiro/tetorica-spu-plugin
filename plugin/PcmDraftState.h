// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuWaveform.h"
#include <cstdint>

namespace tetorica {

// Non-RT editor draft, deliberately separate from the saved instrument. A
// delayed observation of an older publication must not erase a newer Apply.
class PcmDraftState final {
public:
  const PcmCycle& cycle() const noexcept { return mCycle; }

  void set(const PcmCycle& cycle) {
    validatePcmCycle(cycle);
    if(cycle==mCycle)return;
    mCycle=cycle;
    ++mRevision;
  }

  // Call only after the loader accepted a valid request.
  void requested(std::uint64_t generation) noexcept {
    mGeneration=generation;
    mRequested=mCycle;
    mRequestedRevision=mRevision;
  }

  // acceptedCycle is null for Builtin/VAB: the draft then starts from a Sine,
  // independently of the active sample. Host restore always replaces the draft.
  void refresh(const PcmCycle* acceptedCycle,std::uint64_t latest,bool restored) {
    if(restored)mGeneration=0;
    const bool ownRequest=!restored&&mGeneration&&mGeneration==latest;
    if(ownRequest) {
      // A may become visible while B is preparing (or after B fails). Keep B,
      // including newer unapplied edits, until the latest own request is seen.
      if(!acceptedCycle||*acceptedCycle!=mRequested||mRevision!=mRequestedRevision)return;
    }
    const auto next=acceptedCycle?*acceptedCycle:makePcmCycle(WaveformShape::Sine);
    if(next!=mCycle){mCycle=next;++mRevision;}
  }

private:
  PcmCycle mCycle=makePcmCycle(WaveformShape::Sine),mRequested{};
  std::uint64_t mRevision=0,mRequestedRevision=0,mGeneration=0;
};

} // namespace tetorica
