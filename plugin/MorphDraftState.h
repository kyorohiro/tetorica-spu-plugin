// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuMorph.h"
#include <cstdint>

namespace tetorica {

inline bool sameMorphEndpoints(const MorphEndpoints& a,const MorphEndpoints& b) noexcept {
  return a.a==b.a&&a.b==b.b;
}

// Editor-only endpoint pair. Amount/Enabled are independent host parameters;
// a draft never changes either the live instrument or the saved definition.
class MorphDraftState final {
public:
  const MorphEndpoints& endpoints() const noexcept { return mEndpoints; }

  void set(const MorphEndpoints& endpoints) {
    validateMorphEndpoints(endpoints);
    if(sameMorphEndpoints(endpoints,mEndpoints))return;
    mEndpoints=endpoints;++mRevision;
  }

  void requested(std::uint64_t generation) noexcept {
    mGeneration=generation;mRequested=mEndpoints;mRequestedRevision=mRevision;
  }

  void refresh(const MorphEndpoints& accepted,std::uint64_t latest,bool restored) {
    const bool changed=!mObserved||!sameMorphEndpoints(accepted,mObservedEndpoints);
    mObservedEndpoints=accepted;mObserved=true;
    if(restored)mGeneration=0;
    // A later base-source request may inherit our published endpoints before
    // the UI ever observes that publication. Its newer generation does not
    // make the older requested pair newer than edits made after that Apply.
    if(!restored&&mGeneration&&sameMorphEndpoints(accepted,mRequested)&&
        mRevision!=mRequestedRevision)return;
    const bool ownRequest=!restored&&mGeneration&&mGeneration==latest;
    if(ownRequest&&(!sameMorphEndpoints(accepted,mRequested)||mRevision!=mRequestedRevision))return;
    // A base-source selection can retain identical accepted endpoints. It must
    // not erase an unrelated local endpoint draft merely by replacing its bank.
    if(!restored&&!ownRequest&&!changed)return;
    if(!sameMorphEndpoints(accepted,mEndpoints)){mEndpoints=accepted;++mRevision;}
  }

private:
  MorphEndpoints mEndpoints{},mRequested{},mObservedEndpoints{};
  std::uint64_t mRevision=0,mRequestedRevision=0,mGeneration=0;
  bool mObserved=false;
};

} // namespace tetorica
