// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Nonzero PMON coverage through real host MIDI ingress, without a test hook.
#pragma once
#include "reverb-checks.h"

namespace tetorica::test {
template<class Factory> void checkChannelStopsPmon(Factory make,double rate,const char* host) {
  const PmonSettings pmon{true,WaveformShape::Saw,1,.5,0};
  const auto wire=encodeState(InstrumentDefinition::builtin(),100,{}, {},{},pmon);
  auto stopped=make(),held=make(),solo=make();
  for(auto* instance:{stopped.get(),held.get(),solo.get()})
    check(instance->tryStateRestoreForTest(wire),"CC120 nonzero-PMON state rejected");
  // Fill all12 pairs. The surviving first pair occupies the same physical
  // slot in the solo reference. All11 other pairs belong to channel15.
  std::vector<Midi> notes{on(0,60)};
  for(int n=1;n<12;++n)notes.push_back(on(15,48+n));
  stopped->renderMidi(notes);held->renderMidi(notes);solo->renderMidi({on(0,60)});
  for(int n=0;n<12;++n) {
    reverb_detail::exact(*stopped,*held,{},"CC120 nonzero-PMON warmup diverged");
    solo->renderMidi({});
  }
  const auto state=stopped->stateBodyForTest();
  const auto a=stopped->renderMidi({cc(120,0,15,73)}),ar=stopped->right();
  const auto b=held->renderMidi({}),br=held->right();
  const auto c=solo->renderMidi({}),cr=solo->right();
  const auto settle=rate==44100?std::size_t{0}:
    static_cast<std::size_t>(std::ceil(2*rate/44100.))+2;
  check(delta({a.begin(),a.begin()+73},{b.begin(),b.begin()+73})<1e-7&&
    delta({ar.begin(),ar.begin()+73},{br.begin(),br.begin()+73})<1e-7,
    "CC120 changed nonzero-PMON audio before its offset");
  check(delta({a.begin()+73+settle,a.end()},{c.begin()+73+settle,c.end()})<1e-7&&
    delta({ar.begin()+73+settle,ar.end()},{cr.begin()+73+settle,cr.end()})<1e-7,
    "CC120 failed to stop all target PMON pairs or changed the surviving pair");
  for(int n=0;n<8;++n)
    reverb_detail::exact(*stopped,*solo,{},"CC120 disturbed a surviving nonzero-PMON pair");
  // With all pairs initially occupied, the next key reuses the first stopped
  // pair. Compare it against that physical pair's first-ever key in the solo
  // reference: a stale carrier/modulator or failed fresh latch changes PCM.
  reverb_detail::exact(*stopped,*solo,{on(15,72,37)},
    "CC120 PMON pair reuse retained an old carrier/modulator phase or envelope");
  for(int n=0;n<12;++n)
    reverb_detail::exact(*stopped,*solo,{},"Reused CC120 PMON pair PCM diverged");
  check(maximum(stopped->right())>1e-4&&stopped->stateBodyForTest()==state,
    "CC120 PMON pair reuse failed to sound or changed authored state");
  std::cout<<"PASS "<<host<<" CC120 nonzeroPMON sampleRate="<<rate
    <<" twelvePairs=true channel15Targets=true channel0SurvivorPCM=true offsetPrefix=true"
    <<" stoppedPairReuseFresh=true authoredStateUnchanged=true\n";
}
} // namespace tetorica::test
