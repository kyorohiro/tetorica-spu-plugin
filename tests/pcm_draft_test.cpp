// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "../plugin/PcmDraftState.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {
  if(!value)throw std::runtime_error(message);
}
}

int main() {
  try {
    using namespace tetorica;
    const auto sine=makePcmCycle(WaveformShape::Sine);
    const auto a=makePcmCycle(WaveformShape::Triangle);
    const auto b=makePcmCycle(WaveformShape::Saw);
    const auto c=makePcmCycle(WaveformShape::Square);
    PcmDraftState draft;
    require(draft.cycle()==sine,"Initial draft is not Sine");

    // Audio adopts A before the UI timer sees it. Apply B from that stale UI,
    // then observe A while B is still preparing. B must survive both refreshes.
    draft.set(a);draft.requested(1);
    draft.set(b);draft.requested(2);
    draft.refresh(&a,2,false);
    require(draft.cycle()==b,"Intermediate adopted A erased newer requested B");
    draft.refresh(&b,2,false);
    require(draft.cycle()==b,"Completed B left a stale draft after two Applies");

    // B may fail while A remains active. Keep B as an unapplied draft instead
    // of silently presenting A as the user's most recent edit.
    PcmDraftState failed;
    failed.set(a);failed.requested(1);failed.set(b);failed.requested(2);
    failed.refresh(&a,2,false);failed.refresh(&a,2,false);
    require(failed.cycle()==b,"Failed latest request discarded its editable draft");

    // An uncommitted C made after Apply B is newer than every audio snapshot.
    PcmDraftState edited;
    edited.set(a);edited.requested(1);edited.set(b);edited.requested(2);edited.set(c);
    edited.refresh(&a,2,false);edited.refresh(&b,2,false);
    require(edited.cycle()==c,"Completed request erased a newer unapplied edit");

    // Host restore has priority over both pending and unapplied UI work, even
    // when the restored cycle equals an earlier local request.
    edited.refresh(&a,3,true);
    require(edited.cycle()==a,"Host PCM restore retained an unrelated draft");
    edited.set(c);edited.refresh(nullptr,4,true);
    require(edited.cycle()==sine,"Host Builtin/VAB restore did not seed independent Sine");

    // A newer explicit Builtin/VAB request invalidates the old PCM ownership.
    draft.set(c);draft.requested(5);draft.refresh(nullptr,6,false);
    require(draft.cycle()==sine,"Explicit non-PCM source change kept the old draft");
    draft.set(c);draft.refresh(&b,7,false);
    require(draft.cycle()==b,"External accepted PCM was hidden by stale request ownership");

    // Initial/reopened editor follows an applied cycle without issuing a load.
    PcmDraftState reopened;reopened.refresh(&b,8,false);
    require(reopened.cycle()==b,"Editor did not display the accepted PCM cycle");
    auto invalid=b;invalid[57]=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{reopened.set(invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&reopened.cycle()==b,"Invalid local edit corrupted the draft");
    std::cout<<"PASS PCM draft twoApplyIntermediate=true failedRequestDraft=true newerUnapplied=true hostRestore=true sourceChange=true invalidAtomic=true\n";
  } catch(const std::exception& error) {
    std::cerr<<"FAIL PCM draft: "<<error.what()<<'\n';return 1;
  }
}
