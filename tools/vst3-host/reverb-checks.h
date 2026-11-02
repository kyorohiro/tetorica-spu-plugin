// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Shared assertions through real AU parameters/VST3 parameter queues and PCM.
#pragma once
#include "envelope-checks.h"
#include <cstring>
#include <limits>

namespace tetorica::test {
namespace reverb_detail {
using Bytes=std::vector<std::uint8_t>;
inline void requireReverb(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
inline bool same(ReverbSettings a,ReverbSettings b) {
  return a.preset==b.preset&&a.enabled==b.enabled&&a.useToneSend==b.useToneSend&&
    a.volumeLeft==b.volumeLeft&&a.volumeRight==b.volumeRight;
}
inline void putDouble(Bytes& bytes,std::size_t offset,double value) {
  std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
  for(unsigned i=0;i<8;++i)bytes.at(offset+i)=static_cast<std::uint8_t>(bits>>(i*8));
}
template<class Instance> void author(Instance& instance,ReverbSettings effects) {
  instance.plainParameterForTest(12,static_cast<std::uint32_t>(effects.preset));
  instance.plainParameterForTest(13,effects.enabled?1:0);
  instance.plainParameterForTest(14,effects.volumeLeft*100.);
  instance.plainParameterForTest(15,effects.volumeRight*100.);
  instance.plainParameterForTest(16,effects.useToneSend?1:0);
}
struct PCM {std::vector<float> left,right;};
template<class Instance> PCM reverbBlocks(Instance& instance,int count) {
  PCM result;
  for(int i=0;i<count;++i) {
    const auto left=instance.renderMidi({});result.left.insert(result.left.end(),left.begin(),left.end());
    const auto& right=instance.right();result.right.insert(result.right.end(),right.begin(),right.end());
  }
  return result;
}
template<class A,class B> void exact(A& a,B& b,const std::vector<Midi>& events,const char* message) {
  const auto left=a.renderMidi(events);const auto reference=b.renderMidi(events);
  requireReverb(left==reference&&a.right()==b.right(),message);
}
}

template<class Factory> void checkReverbs(Factory make,double rate,const char* host,
                                        std::vector<std::uint8_t> bank) {
  using namespace reverb_detail;
  using envelope_detail::word;
  const auto builtin=InstrumentDefinition::builtin();
  const ReverbSettings room{ReverbPreset::Room,true,false,1,1};
  const auto on=std::vector<Midi>{tetorica::test::on(0,60,37)};
  const auto off=std::vector<Midi>{tetorica::test::off(0,60)};
  const int heldBlocks=static_cast<int>(std::ceil(rate*.5/256));
  const int tailBlocks=static_cast<int>(std::ceil(rate*.5/256));

  // Automating a non-Off preset alone must not implicitly enable the effect.
  auto automated=make(),dry=make();
  automated->plainParameterForTest(12,1);
  exact(*automated,*dry,on,"Preset-only automation changed disabled Reverb PCM");
  auto snapshot=envelope_detail::saved(*automated);
  requireReverb(snapshot.reverb.preset==ReverbPreset::Room&&!snapshot.reverb.enabled,
    "Preset automation unexpectedly enabled Reverb");

  // Explicit Enable, zero return, and source-send gates remain independent.
  for(int gate=0;gate<3;++gate) {
    auto gated=make(),reference=make();auto settings=room;
    if(gate==0)settings.enabled=false;
    if(gate==1)settings.volumeLeft=settings.volumeRight=0;
    if(gate==2)settings.useToneSend=true; // Builtin source has no send flag.
    author(*gated,settings);
    exact(*gated,*reference,on,"Disabled/zero-return/Tone-send gate changed dry PCM");
    for(int i=0;i<heldBlocks;++i)exact(*gated,*reference,{},"Reverb dry gate changed held PCM");
    exact(*gated,*reference,off,"Reverb dry gate changed released PCM");
    for(int i=0;i<tailBlocks;++i)exact(*gated,*reference,{},"Reverb dry gate leaked a wet tail");
  }

  // Each stereo return scales only its wet output. The unchanged channel is
  // bit-identical to dry, including after NoteOff; dry itself remains audible.
  auto left=make(),right=make(),reference=make();
  auto leftSettings=room;leftSettings.volumeRight=0;
  auto rightSettings=room;rightSettings.volumeLeft=0;
  author(*left,leftSettings);author(*right,rightSettings);
  left->renderMidi(on);right->renderMidi(on);reference->renderMidi(on);
  const auto a=reverbBlocks(*left,heldBlocks),b=reverbBlocks(*right,heldBlocks),plain=reverbBlocks(*reference,heldBlocks);
  requireReverb(a.right==plain.right&&b.left==plain.left,"Stereo Reverb return changed the opposite dry channel");
  requireReverb(delta(a.left,plain.left)>1e-5&&delta(b.right,plain.right)>1e-5,
    "Independent L/R Reverb return automation had no wet effect");
  left->renderMidi(off);right->renderMidi(off);reference->renderMidi(off);
  const auto leftTail=reverbBlocks(*left,tailBlocks),rightTail=reverbBlocks(*right,tailBlocks),dryTail=reverbBlocks(*reference,tailBlocks);
  const auto settle=static_cast<std::size_t>(std::ceil(rate/44100.*2048));
  requireReverb(envelope_detail::envelopePeak(dryTail.left,settle)==0&&
    envelope_detail::envelopePeak(leftTail.left,settle)>1e-6&&envelope_detail::envelopePeak(rightTail.right,settle)>1e-6,
    "Reverb did not retain a nonzero tail after dry release ended");

  auto roomVoice=make(),echoVoice=make(),echoReference=make();auto echo=room;echo.preset=ReverbPreset::SpaceEcho;
  author(*roomVoice,room);author(*echoVoice,echo);author(*echoReference,echo);
  roomVoice->renderMidi(on);echoVoice->renderMidi(on);echoReference->renderMidi(on);
  const auto roomPCM=reverbBlocks(*roomVoice,heldBlocks),echoPCM=reverbBlocks(*echoVoice,heldBlocks);
  const auto echoReferencePCM=reverbBlocks(*echoReference,heldBlocks);
  requireReverb(echoPCM.left==echoReferencePCM.left&&echoPCM.right==echoReferencePCM.right,
    "CC120 Reverb reference warmup diverged");
  requireReverb(delta(roomPCM.left,echoPCM.left)>1e-5,"Distinct Reverb presets produced identical PCM");
  // Disable mutes wet immediately and lets processing decay silently. A MIDI
  // channel's CC120 must retain the shared effect and interpolation history;
  // the explicit Off preset still discards all history.
  roomVoice->renderMidi(off);roomVoice->plainParameterForTest(13,0);
  const auto muted=reverbBlocks(*roomVoice,tailBlocks);
  requireReverb(envelope_detail::envelopePeak(muted.left,settle)==0,"Enable-off leaked wet output");
  exact(*echoVoice,*echoReference,off,"CC120 Reverb release references diverged");
  // After0.1s the Builtin dry release has ended; nonzero output is previously
  // accumulated wet history. Stopping its MIDI channel cannot erase that bus.
  for(int n=0;n<static_cast<int>(std::ceil(rate*.1/256));++n)
    exact(*echoVoice,*echoReference,{},"CC120 Reverb tail preparation diverged");
  const auto stoppedTail=echoVoice->renderMidi({cc(120,0,0,73)});
  const auto retainedTail=echoReference->renderMidi({});
  requireReverb(stoppedTail==retainedTail&&echoVoice->right()==echoReference->right(),
    "CC120 cleared or interrupted the shared Reverb/rate-adapter history");
  const auto remaining=reverbBlocks(*echoVoice,tailBlocks),remainingReference=reverbBlocks(*echoReference,tailBlocks);
  requireReverb(remaining.left==remainingReference.left&&remaining.right==remainingReference.right&&
    envelope_detail::envelopePeak(remaining.left)>1e-6,
    "CC120 did not preserve a nonzero shared Reverb tail");
  roomVoice->plainParameterForTest(12,0);roomVoice->renderMidi({});
  author(*roomVoice,room);
  requireReverb(envelope_detail::envelopePeak(reverbBlocks(*roomVoice,tailBlocks).left)==0,"Off preset did not clear Reverb history");

  // Preserve every authored parameter alongside original source bytes. Source
  // Tone1 requests send, so useToneSend=true exercises its retained metadata.
  bank.at(0x840+1)|=4;
  const auto definition=InstrumentDefinition::fromVab(bank,3,1);
  const EnvelopeSettings envelope{{0xa937,0xd7bb},false,false};
  const ReverbSettings authored{ReverbPreset::Room,true,true,.375,.8125};
  const auto full=encodeState(definition,37,envelope,authored);
  auto state=make(),stateReference=make();
  requireReverb(state->tryStateRestoreForTest(encodeState(definition,37)),"Reverb source restore failed");
  envelope_detail::author(*state,envelope);author(*state,authored);state->renderMidi({});
  requireReverb(state->stateBodyForTest()==full,"Full saved parameter state did not preserve exact authored values");
  requireReverb(state->tryStateRestoreForTest(full)&&stateReference->tryStateRestoreForTest(full),"Authored wet state rejected");
  // Validate malformed state while the valid replacement is still pending.
  std::vector<Bytes> malformed;
  auto bad=full;word(bad,48+bank.size(),8);malformed.push_back(bad);
  bad=full;word(bad,52+bank.size(),4);malformed.push_back(bad);
  for(auto offset:{56u,64u})for(double value:{-.1,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    bad=full;putDouble(bad,offset+bank.size(),value);malformed.push_back(bad);
  }
  bad=full;bad.pop_back();malformed.push_back(bad);
  for(const auto& invalid:malformed) {
    requireReverb(!state->tryStateRestoreForTest(invalid)&&state->stateBodyForTest()==full,
      "Malformed Reverb state changed parameters/source/pending replacement");
  }
  exact(*state,*stateReference,on,"First post-restore wet PCM differs from independent instance");
  const auto held=reverbBlocks(*state,heldBlocks),heldReference=reverbBlocks(*stateReference,heldBlocks);
  requireReverb(held.left==heldReference.left&&held.right==heldReference.right,"Restored wet PCM diverged");
  for(const auto& invalid:malformed) {
    requireReverb(!state->tryStateRestoreForTest(invalid)&&state->stateBodyForTest()==full,"Rejected Reverb state mutated active authoring state");
    exact(*state,*stateReference,{},"Rejected Reverb state interrupted held wet PCM");
  }
  // Restoring also discards the previous wet history, without losing a fresh
  // current-block NoteOn. Compare to a newly initialized identical state.
  auto fresh=make();requireReverb(fresh->tryStateRestoreForTest(full)&&state->tryStateRestoreForTest(full),"Wet re-restore failed");
  exact(*state,*fresh,on,"Old wet history leaked into the first post-restore block");
  for(int i=0;i<heldBlocks;++i)exact(*state,*fresh,{},"Old wet history survived state restore");
  for(unsigned schema:{1u,2u,3u}) {
    auto old=full;old.resize(bank.size()+(schema==1?40:schema==2?48:72));word(old,8,schema);word(old,12,static_cast<std::uint32_t>(old.size()));
    requireReverb(state->tryStateRestoreForTest(old),"Old schema Reverb migration rejected");
    snapshot=envelope_detail::saved(*state);
    const auto expectedEffects=schema>=3?authored:ReverbSettings{};
    requireReverb(same(snapshot.reverb,expectedEffects)&&snapshot.definition->bankBytes()==bank&&
      envelope_detail::equal(snapshot.envelope,schema==1?EnvelopeSettings{}:envelope),"Old schema did not migrate to disabled Reverb with source/ADSR retained");
    requireReverb(fresh->tryStateRestoreForTest(encodeState(definition,37,schema==1?EnvelopeSettings{}:envelope,expectedEffects)),"Migration reference rejected");
    exact(*state,*fresh,on,"Old schema dry first PCM changed after Reverb migration");
  }
  Bytes legacy(8);putDouble(legacy,0,17);
  requireReverb(state->tryStateRestoreForTest(full)&&state->tryStateRestoreForTest(legacy),"Legacy restore over wet state rejected");
  snapshot=envelope_detail::saved(*state);
  requireReverb(same(snapshot.reverb,{})&&envelope_detail::equal(snapshot.envelope,{})&&
    snapshot.definition->kind()==InstrumentDefinition::Kind::Builtin&&snapshot.gainPercent==17,"Legacy migration retained Reverb settings");
  std::cout<<"PASS "<<host<<" Reverb sampleRate="<<rate
    <<" full27Params=true firstWetRestore=true sourceSend=true presetAutomationIndependent=true presetsDiffer=true"
    <<" disabledDry=true zeroReturnDry=true stereoReturns=true releasedTail=true CC120TailRetained=true offClears=true"
    <<" restoreClearsHistory=true malformedAtomic=true schema1Schema2Schema3Legacy=true\n";
}
} // namespace tetorica::test
