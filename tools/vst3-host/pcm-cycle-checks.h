// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual AU/VST3 state and MIDI tests for the authored PCM source.
#pragma once
#include "reverb-checks.h"

namespace tetorica::test {
template<class Factory> void checkPcmCycles(Factory make,double rate,const char* host) {
  using Bytes=std::vector<std::uint8_t>;
  const auto requirePCM=[](bool value,const char* message) {if(!value)throw std::runtime_error(message);};
  const EnvelopeSettings envelope{{0x000f,0x1fc0},true,false};
  const ReverbSettings effects{ReverbPreset::Room,false,false,.375,.625};
  const auto on=std::vector<Midi>{tetorica::test::on(0,60,37)};
  const auto word=envelope_detail::word;
  std::vector<std::vector<float>> shapes;
  for(auto shape:{WaveformShape::Sine,WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square}) {
    const auto cycle=makePcmCycle(shape);
    const auto definition=InstrumentDefinition::fromPcmCycle(cycle);
    const auto wire=encodeState(definition,37,envelope,effects);
    auto first=make(),second=make(),isolated=make();
    requirePCM(first->tryStateRestoreForTest(wire)&&second->tryStateRestoreForTest(wire),"PCM cycle host restore rejected");
    requirePCM(first->stateBodyForTest()==wire,"PCM source/parameters changed before audio adoption");
    const auto decoded=envelope_detail::saved(*first);
    requirePCM(decoded.definition->kind()==InstrumentDefinition::Kind::PcmCycle&&decoded.definition->pcmCycle()==cycle&&
      decoded.gainPercent==37&&envelope_detail::equal(decoded.envelope,envelope)&&reverb_detail::same(decoded.reverb,effects),
      "PCM authoring data or independent parameter settings failed round-trip");
    const auto a=first->renderMidi(on),b=second->renderMidi(on);
    requirePCM(a==b&&first->right()==second->right(),"PCM first NoteOn block differed between restored instances");
    requirePCM(std::all_of(a.begin(),a.begin()+37,[](float value){return value==0;})&&maximum(a)>1e-4,
      "PCM source discarded or mistimed the first post-restore NoteOn");
    std::vector<float> sound;
    for(int i=0;i<64;++i) {
      const auto x=first->renderMidi({}),y=second->renderMidi({});
      requirePCM(x==y&&first->right()==second->right(),"PCM cycle loop playback diverged after restore");
      sound.insert(sound.end(),x.begin(),x.end());
    }
    requirePCM(std::abs(frequency(sound,rate)-261.625565)<1.,"PCM cycle note pitch/loop period is incorrect");
    requirePCM(first->stateBodyForTest()==wire,"Playback mutated editable PCM authoring data");
    requirePCM(maximum(isolated->renderMidi({}))==0&&
      envelope_detail::saved(*isolated).definition->kind()==InstrumentDefinition::Kind::BuiltinSineWide,
      "PCM cycle restore affected a separate instance");
    shapes.push_back(std::move(sound));
  }
  for(std::size_t a=0;a<shapes.size();++a)for(std::size_t b=a+1;b<shapes.size();++b)
    requirePCM(delta(shapes[a],shapes[b])>1e-3,"Distinct PCM waveform choices produced identical audio");

  // A hand-edited asymmetric cycle is preserved exactly, independently from
  // the built-in menu shapes. All source validation precedes transactional commit.
  auto custom=makePcmCycle(WaveformShape::Triangle);
  for(std::size_t i=0;i<custom.size();++i)custom[i]=custom[i]*.7f+(i%17==0?.06f:0.f);
  custom[0]=-.0f;custom[111]=.125f;
  const auto wire=encodeState(InstrumentDefinition::fromPcmCycle(custom),37,envelope,effects);
  auto state=make(),reference=make();
  requirePCM(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"Drawn PCM state restore failed");
  std::vector<Bytes> malformed;
  for(auto bits:{0x7f800000u,0x7fc00000u,0xbf800001u,0x3f800001u})for(auto index:{0u,111u}) {
    auto bad=wire;word(bad,32+4*index,bits);malformed.push_back(bad);
  }
  for(auto index:{20u,24u}) {auto bad=wire;word(bad,index,1);malformed.push_back(bad);}
  for(auto length:{0u,447u,449u}) {auto bad=wire;word(bad,28,length);malformed.push_back(bad);}
  auto bad=wire;word(bad,8,3);malformed.push_back(bad); //Kind2 was not defined in schema3.
  bad=wire;bad.pop_back();malformed.push_back(bad);
  for(const auto& invalid:malformed)
    requirePCM(!state->tryStateRestoreForTest(invalid)&&state->stateBodyForTest()==wire,
      "Malformed PCM state changed a pending source or parameter tuple");
  reverb_detail::exact(*state,*reference,on,"Drawn PCM first restore block changed after rejected input");
  for(const auto& invalid:malformed) {
    requirePCM(!state->tryStateRestoreForTest(invalid)&&state->stateBodyForTest()==wire,
      "Malformed PCM state changed the active source/parameters");
    reverb_detail::exact(*state,*reference,{},"Rejected PCM state interrupted held-note audio");
  }
  // Replace held and future carried old notes with a custom cycle. The next
  // block keeps its own fresh NoteOn while all previous-source notes disappear.
  auto replacing=make(),fresh=make();replacing->renderMidi({tetorica::test::on()});
  replacing->renderMidi({tetorica::test::on(0,67,256)});
  requirePCM(replacing->tryStateRestoreForTest(wire)&&fresh->tryStateRestoreForTest(wire),"Active source replacement failed");
  reverb_detail::exact(*replacing,*fresh,on,"PCM source replacement retained old held/carried notes");
  const auto snapshot=envelope_detail::saved(*replacing);
  requirePCM(snapshot.definition->pcmCycle()==custom&&replacing->stateBodyForTest()==wire,
    "Custom PCM source failed exact save after active replacement");
  std::cout<<"PASS "<<host<<" PCM cycles sampleRate="<<rate
    <<" shapes4Distinct=true pitchAndLoop=true samples112Exact=true customDrawnState=true"
    <<" firstRestoreBlock=true pendingActiveAtomic=true separateInstances=true heldCarriedRestore=true settingsIndependent=true\n";
}
} // namespace tetorica::test
