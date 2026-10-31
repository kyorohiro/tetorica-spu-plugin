// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "../plugin/MorphDraftState.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
}
int main() {
  try {
    using namespace tetorica;
    const MorphEndpoints initial{};
    const MorphEndpoints a{makePcmCycle(WaveformShape::Triangle),makePcmCycle(WaveformShape::Square)};
    const MorphEndpoints b{makePcmCycle(WaveformShape::Saw),makePcmCycle(WaveformShape::Sine)};
    const MorphEndpoints c{makePcmCycle(WaveformShape::Square),makePcmCycle(WaveformShape::Triangle)};
    MorphDraftState draft;
    require(sameMorphEndpoints(draft.endpoints(),initial),"Wrong initial endpoint draft");
    draft.refresh(initial,0,false);
    draft.set(a);draft.requested(1);draft.set(b);draft.requested(2);
    draft.refresh(a,2,false);
    require(sameMorphEndpoints(draft.endpoints(),b),"Intermediate Apply A erased pending B");
    draft.refresh(b,2,false);
    require(sameMorphEndpoints(draft.endpoints(),b),"Accepted B left an old pair visible");

    draft.set(a);draft.requested(3);draft.set(b);draft.requested(4);draft.set(c);
    draft.refresh(a,4,false);draft.refresh(b,4,false);
    require(sameMorphEndpoints(draft.endpoints(),c),"Accepted endpoints erased a newer local draft");
    draft.refresh(b,5,false);
    require(sameMorphEndpoints(draft.endpoints(),c),"Base source change erased retained endpoint draft");
    draft.refresh(b,6,true);
    require(sameMorphEndpoints(draft.endpoints(),b),"Host restore did not replace endpoint draft");
    draft.set(c);draft.refresh(b,7,true);
    require(sameMorphEndpoints(draft.endpoints(),b),"Equal endpoint restore did not discard local edits");

    // B is published, but not observed, before a Builtin request inherits B.
    // Its newer generation must not make that first B snapshot erase local C.
    MorphDraftState delayed;delayed.refresh(a,0,false);
    delayed.set(b);delayed.requested(1);delayed.set(c);
    delayed.refresh(b,2,false);
    require(sameMorphEndpoints(delayed.endpoints(),c),"Delayed B observation through a base request erased newer C");
    delayed.refresh(b,2,false);
    require(sameMorphEndpoints(delayed.endpoints(),c),"Repeated retained B observation erased newer C");
    delayed.refresh(b,3,true);
    require(sameMorphEndpoints(delayed.endpoints(),b),"Host restore did not override delayed local ownership");

    MorphDraftState failed;failed.refresh(initial,0,false);
    failed.set(a);failed.requested(8);failed.refresh(initial,8,false);
    require(sameMorphEndpoints(failed.endpoints(),a),"Failed Apply discarded its editable pair");
    failed.refresh(b,9,false);
    require(sameMorphEndpoints(failed.endpoints(),b),"External endpoints remained hidden by stale ownership");
    auto invalid=c;invalid.b[19]=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{failed.set(invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&sameMorphEndpoints(failed.endpoints(),b),"Invalid B partially changed draft A/B");
    std::cout<<"PASS Morph draft twoApply=true newerUnapplied=true baseRetained=true delayedBaseObservation=true hostRestore=true failedRequest=true invalidAtomic=true\n";
  }catch(const std::exception& error){std::cerr<<"FAIL Morph draft: "<<error.what()<<'\n';return 1;}
}
