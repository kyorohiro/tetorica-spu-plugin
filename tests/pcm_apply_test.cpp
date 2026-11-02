// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "../plugin/PcmApplyState.h"
#include "../plugin/PcmDraftState.h"
#include "../plugin/MorphDraftState.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
}

int main() {
  try {
    using namespace tetorica;
    using tetorica::ui::PcmApplyState;
    using Target = PcmApplyState::Target;
    const auto a = makePcmCycle(WaveformShape::Sine);
    const auto b = makePcmCycle(WaveformShape::Triangle);
    const auto c = makePcmCycle(WaveformShape::Square);
    const MorphEndpoints endpointsA{a, b}, endpointsB{b, c};
    PcmApplyState apply;
    require(apply.enabled() && !apply.autoApply(Target::Pcm) &&
      !apply.autoApply(Target::Morph), "Opening an editor would replace its untouched source");
    apply.edited(Target::Pcm);
    require(apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph),
      "Completed PCM edit did not request default automatic apply");
    apply.requested(1, b);
    require(!apply.requestNeeded(b, false, 1, true), "Duplicate pending PCM restarted preparation");
    require(apply.requestNeeded(a, true, 1, true), "A -> B -> A did not supersede pending B");
    apply.requested(2, a);
    require(!apply.requestNeeded(a, true, 2, true), "Repeated return to A restarted preparation");
    require(apply.requestNeeded(c, false, 2, true), "Newer PCM edit was ignored");
    require(apply.requestNeeded(a, true, 3, true), "External pending source was mistaken for our request");
    require(!apply.requestNeeded(a, true, 2, false), "An applied cycle restarted without a pending change");
    require(apply.requestNeeded(b, false, 1, false), "Failed request could not be explicitly retried");

    apply.setEnabled(false);
    apply.edited(Target::Pcm); apply.edited(Target::Morph);
    require(!apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph), "Manual drafts still auto-applied");
    apply.setEnabled(true);
    require(apply.autoApply(Target::Pcm) && apply.autoApply(Target::Morph), "Re-enabling lost intentional manual edits");
    apply.synchronized(Target::Pcm);
    require(!apply.autoApply(Target::Pcm) && apply.autoApply(Target::Morph), "Accepting PCM erased a separate Morph draft");
    apply.requested(4, endpointsA);
    require(!apply.requestNeeded(endpointsA, false, 4, true), "Duplicate endpoint Apply restarted preparation");
    require(apply.requestNeeded(endpointsB, false, 4, true), "Newer endpoint pair was ignored");
    require(apply.requestNeeded(a, true, 4, true), "PCM choice failed to supersede pending endpoints");
    apply.restored();
    require(apply.enabled() && !apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph) &&
      !apply.sameRequest(endpointsA, 4), "Restore retained stale edit intent or request ownership");
    apply.setEnabled(false); apply.restored();
    require(!apply.enabled(), "Restore changed the editor's Auto apply preference");

    // Auto apply uses the same draft ownership as manual Apply. Intermediate
    // worker observations must never erase a more recent completed stroke.
    PcmDraftState pcm;
    pcm.set(b); pcm.requested(5); pcm.set(c); pcm.requested(6);
    pcm.refresh(&b, 6, false);
    require(pcm.cycle() == c, "Intermediate automatic PCM erased the latest edit");
    MorphDraftState morph;
    morph.set(endpointsA); morph.requested(7); morph.set(endpointsB); morph.requested(8);
    morph.refresh(endpointsA, 8, false);
    require(sameMorphEndpoints(morph.endpoints(), endpointsB), "Intermediate automatic endpoints erased the latest edit");
    std::cout << "PASS PCM apply defaultOn=true untouchedSource=true duplicateSuppression=true returnToActive=true manualDraft=true explicitRetry=true restore=true newestEdit=true\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL PCM apply: " << error.what() << '\n'; return 1;
  }
}
