// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuMorph.h"
#include <cstdint>

namespace tetorica::ui {

// Editor-only intent, separate from DAW parameters and the saved instrument.
// Only an explicit edit or enabling Auto apply can request work. Observing a
// worker completion never retries a failed request or reapplies an old draft.
class PcmApplyState final {
public:
  enum class Target { Pcm, Morph };

  bool enabled() const noexcept { return mEnabled; }
  void setEnabled(bool enabled) noexcept { mEnabled = enabled; }
  void edited(Target target) noexcept { editedFlag(target) = true; }
  void synchronized(Target target) noexcept { editedFlag(target) = false; }
  bool autoApply(Target target) const noexcept {
    return mEnabled && (target == Target::Pcm ? mPcmEdited : mMorphEdited);
  }

  void restored() noexcept {
    mPcmEdited = mMorphEdited = false;
    mGeneration = 0;
  }

  bool sameRequest(const PcmCycle& cycle, std::uint64_t latest) const noexcept {
    return mGeneration && mGeneration == latest && mTarget == Target::Pcm &&
      mCycle == cycle;
  }
  bool sameRequest(const MorphEndpoints& endpoints, std::uint64_t latest) const noexcept {
    return mGeneration && mGeneration == latest && mTarget == Target::Morph &&
      mEndpoints.a == endpoints.a && mEndpoints.b == endpoints.b;
  }

  // A pending different request must be superseded even when the user's new
  // choice equals the still-active sound (A -> B -> A before B is adopted).
  template<class Draft>
  bool requestNeeded(const Draft& draft, bool alreadyApplied,
                     std::uint64_t latest, bool pending) const noexcept {
    return pending ? !sameRequest(draft, latest) : !alreadyApplied;
  }

  void requested(std::uint64_t generation, const PcmCycle& cycle) noexcept {
    mGeneration = generation; mTarget = Target::Pcm; mCycle = cycle;
  }
  void requested(std::uint64_t generation, const MorphEndpoints& endpoints) noexcept {
    mGeneration = generation; mTarget = Target::Morph; mEndpoints = endpoints;
  }

private:
  bool& editedFlag(Target target) noexcept {
    return target == Target::Pcm ? mPcmEdited : mMorphEdited;
  }
  bool mEnabled = true, mPcmEdited = false, mMorphEdited = false;
  Target mTarget = Target::Pcm;
  std::uint64_t mGeneration = 0;
  PcmCycle mCycle{};
  MorphEndpoints mEndpoints{};
};

} // namespace tetorica::ui
