// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual AU/VST3 parameters/state/MIDI. No production test hooks or editor.
#pragma once
#include "pmon-checks.h"

namespace tetorica::test {
namespace morph_detail {
inline bool same(const MorphEndpoints& a,const MorphEndpoints& b) {return a.a==b.a&&a.b==b.b;}
template<class Instance> void author(Instance& instance,MorphSettings settings) {
  instance.plainParameterForTest(24,settings.enabled?1:0);
  instance.plainParameterForTest(25,settings.amount);
}
}
template<class Factory> void checkMorph(Factory make,double rate,const char* host,const std::vector<std::uint8_t>& bank) {
  using Bytes=std::vector<std::uint8_t>;
  const auto requireMorph=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  const auto builtin=InstrumentDefinition::builtin();
  const auto pcm=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  const auto vab=InstrumentDefinition::fromVab(bank,3,1);
  // A binary32-only endpoint edit must survive even if float equality would
  // consider the restored pair equal to Builtin's default endpoints.
  auto negativeZero=encodeState(builtin,100);
  envelope_detail::word(negativeZero,128,0x80000000u);
  auto exactEndpoints=make();
  requireMorph(exactEndpoints->tryStateRestoreForTest(negativeZero)&&
    exactEndpoints->stateBodyForTest()==negativeZero,"Morph signed-zero endpoint bytes were replaced by defaults");
  MorphEndpoints endpoints{makePcmCycle(WaveformShape::Sine),makePcmCycle(WaveformShape::Square)};
  endpoints.a[17]=-.1234567f;endpoints.b[111]=.3456789f;
  const auto source=InstrumentDefinition::withMorphEndpoints(pcm,endpoints);
  const EnvelopeSettings envelope{{0xa937,0xd7bb},false,false};
  const ReverbSettings effects{ReverbPreset::Room,false,false,.375,.625};
  const NoiseSettings noise{false,37};
  const PmonSettings pmon{true,WaveformShape::Saw,2.5,.375,1250};
  const MorphSettings morph{true,.375};
  const auto events=std::vector<Midi>{on(0,60,37)};
  const auto wire=encodeState(source,37,envelope,effects,noise,pmon,morph);
  auto state=make(),reference=make();
  requireMorph(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"Morph state restore failed");
  requireMorph(state->stateBodyForTest()==wire,"Morph restore changed its full26-parameter tuple/endpoints");
  reverb_detail::exact(*state,*reference,events,"Morph settings/endpoints were not applied before first fresh NoteOn");
  requireMorph(maximum(state->right())>1e-4,"Restored PMON+Morph did not sound");
  for(int i=0;i<8;++i)reverb_detail::exact(*state,*reference,{},"Restored PMON+Morph PCM diverged");
  const auto saved=envelope_detail::saved(*state);
  requireMorph(saved.gainPercent==37&&envelope_detail::equal(saved.envelope,envelope)&&reverb_detail::same(saved.reverb,effects)&&
    saved.noise.clock==37&&!saved.noise.enabled&&pmon_detail::same(saved.pmon,pmon)&&saved.morph.enabled&&saved.morph.amount==.375&&
    morph_detail::same(saved.definition->morphEndpoints(),endpoints)&&saved.definition->pcmCycle()==pcm->pcmCycle(),
    "Morph did not retain independent original source/ADSR/FX/Noise/PMON settings");
  auto automated=make();
  requireMorph(automated->tryStateRestoreForTest(encodeState(source,37,envelope,effects,noise,pmon)),"Morph automation fixture rejected");
  morph_detail::author(*automated,morph);automated->renderMidi({});
  requireMorph(automated->stateBodyForTest()==wire,"Morph block automation did not serialize both parameters");

  // Amount can change a held carrier, including its Release tail, without
  // restarting its cursor or ADSR. A->B->A returns to the unedited A timeline.
  // Ignore the first32 host frames after returning: Gaussian4-point/native
  // history plus causal rate history may still contain preceding B, including
  // at96k. The audition KON fade lasts128 native frames and remains detectable.
  const auto cycle=InstrumentDefinition::withMorphEndpoints(builtin,endpoints);
  const EnvelopeSettings longRelease{{0x000f,0x1fdf},true,true};
  const auto heldWire=encodeState(cycle,100,longRelease,{}, {},{},{true,0});
  for(bool released:{false,true}) {
    auto live=make(),unchanged=make();
    requireMorph(live->tryStateRestoreForTest(heldWire)&&unchanged->tryStateRestoreForTest(heldWire),"Morph live fixtures rejected");
    reverb_detail::exact(*live,*unchanged,events,"Morph held fixtures initially diverged");
    for(int i=0;i<16;++i)reverb_detail::exact(*live,*unchanged,{},"Morph held fixtures diverged");
    if(released) {
      reverb_detail::exact(*live,*unchanged,{off()},"Morph release fixtures diverged");
      for(int i=0;i<4;++i)reverb_detail::exact(*live,*unchanged,{},"Morph release tail ended unexpectedly");
    }
    live->plainParameterForTest(25,0);
    reverb_detail::exact(*live,*unchanged,{},"Repeated Morph amount restarted a voice");
    live->plainParameterForTest(25,1);
    const auto b=live->renderMidi({}),a=unchanged->renderMidi({});
    requireMorph(maximum(a)>1e-4&&delta(a,b)>1e-3,"Morph amount did not update a held/released carrier live");
    live->plainParameterForTest(25,0);
    const auto returned=live->renderMidi({}),original=unchanged->renderMidi({});
    const auto& right=live->right();const auto& otherRight=unchanged->right();
    requireMorph(std::equal(returned.begin()+32,returned.end(),original.begin()+32)&&
      std::equal(right.begin()+32,right.end(),otherRight.begin()+32),"Morph amount restarted held/release cursor or ADSR");
    for(int i=0;i<8;++i)reverb_detail::exact(*live,*unchanged,{},"Morph return-to-A phase diverged");
  }
  // Endpoint and midpoint values also survive independent state restoration;
  // host automation uses the accepted immutable A/B, never an editor draft.
  std::vector<std::vector<float>> blends;
  for(double amount:{0.,.5,1.}) {
    auto voice=make(),restored=make();
    requireMorph(voice->tryStateRestoreForTest(heldWire),"Morph blend fixture rejected");
    voice->plainParameterForTest(25,amount);voice->renderMidi({});
    const auto current=voice->stateBodyForTest();
    requireMorph(restored->tryStateRestoreForTest(current)&&voice->tryStateRestoreForTest(current),"Morph blend authored state rejected");
    reverb_detail::exact(*voice,*restored,events,"Restored Morph amount differs from host automation");
    blends.push_back(blocks(*voice,16));
  }
  for(std::size_t i=0;i<blends.size();++i)for(std::size_t j=i+1;j<blends.size();++j)
    requireMorph(delta(blends[i],blends[j])>1e-3,"Morph A/midpoint/B produced identical PCM");

  // Noise hides Morph without deleting its raw enable/endpoints. Amount edits
  // remain authoring changes, but do not panic or perturb a held noise voice.
  auto masked=make(),noiseReference=make();
  const auto maskedWire=encodeState(source,37,envelope,effects,{true,48},pmon,morph);
  requireMorph(masked->tryStateRestoreForTest(maskedWire)&&noiseReference->tryStateRestoreForTest(
    encodeState(source,37,envelope,effects,{true,48},pmon,{})),"Noise/Morph fixtures rejected");
  reverb_detail::exact(*masked,*noiseReference,events,"Morph overrode Noise priority");
  masked->plainParameterForTest(25,1);
  for(int i=0;i<16;++i)reverb_detail::exact(*masked,*noiseReference,{},"Dormant Morph amount reset a held Noise voice");
  masked->plainParameterForTest(17,0);
  auto unmasked=make();
  requireMorph(unmasked->tryStateRestoreForTest(encodeState(source,37,envelope,effects,{false,48},pmon,{true,1})),"Unmasked PMON+Morph fixture rejected");
  reverb_detail::exact(*masked,*unmasked,events,"Noise-off lost retained PMON/Morph settings or fresh NoteOn");

  // Enable/disable panics once, drops the previous source's future notes, and
  // preserves fresh keys plus carried wheel/pedal messages at their offsets.
  for(bool enabled:{false,true}) {
    auto changed=make(),expected=make();
    requireMorph(changed->tryStateRestoreForTest(encodeState(cycle,100,{}, {},{}, {},{!enabled,.375})),"Morph topology source rejected");
    changed->renderMidi({on(),on(15,67,600),bend(12288,15,600),cc(64,127,15,600)});
    changed->plainParameterForTest(24,enabled?1:0);
    requireMorph(expected->tryStateRestoreForTest(encodeState(cycle,100,{}, {},{}, {},{enabled,.375})),"Morph topology reference rejected");
    reverb_detail::exact(*changed,*expected,{},"Morph toggle retained the previous held voice");
    const auto x=changed->renderMidi({on(15,60,37),off(15,60,200)});
    const auto y=expected->renderMidi({on(15,60,37),bend(12288,15,88),cc(64,127,15,88),off(15,60,200)});
    requireMorph(x==y&&changed->right()==expected->right(),"Morph toggle lost fresh key/carried controllers or replayed an old carried note");
    for(int i=0;i<16;++i)reverb_detail::exact(*changed,*expected,{},"Morph toggle controller timeline diverged");
    requireMorph(maximum(changed->right())>1e-4,"Morph toggle lost the carried sustain pedal");
    reverb_detail::exact(*changed,*expected,{cc(64,0,15,31)},"Morph toggle pedal-up diverged");
  }

  requireMorph(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"Morph atomic fixtures rejected");
  std::vector<Bytes> invalid;
  for(auto field:{112u,116u})for(auto value:{2u,0xffffffffu}) {
    auto bad=wire;envelope_detail::word(bad,field+448,value);invalid.push_back(bad);
  }
  for(auto value:{-.01,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    auto bad=wire;reverb_detail::putDouble(bad,120+448,value);invalid.push_back(bad);
  }
  for(auto field:{128u,576u})for(auto bits:{0x7f800000u,0x7fc00000u,0xbf800001u,0x3f800001u}) {
    auto bad=wire;envelope_detail::word(bad,field+448+111*4,bits);invalid.push_back(bad);
  }
  auto bad=wire;bad.pop_back();invalid.push_back(bad);
  bad=wire;envelope_detail::word(bad,8,8);invalid.push_back(bad);
  for(const auto& bytes:invalid)requireMorph(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,
    "Malformed Morph state changed pending source/endpoints/parameters");
  reverb_detail::exact(*state,*reference,events,"Malformed Morph state changed the first restored note");
  for(const auto& bytes:invalid) {
    requireMorph(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,"Malformed Morph state changed active source/endpoints/parameters");
    reverb_detail::exact(*state,*reference,{},"Malformed Morph state interrupted held audio");
  }
  for(unsigned schema=1;schema<=6;++schema) {
    const auto base=schema>=4?pcm:vab;const auto definition=InstrumentDefinition::withMorphEndpoints(base,endpoints);
    const auto size=schema>=4?448:bank.size();
    auto old=encodeState(definition,37,envelope,effects,{true,37},pmon,morph);
    old.resize(size+(schema==1?40:schema==2?48:schema<=4?72:schema==5?80:112));
    envelope_detail::word(old,8,schema);envelope_detail::word(old,12,static_cast<std::uint32_t>(old.size()));
    const auto oldEnvelope=schema>=2?envelope:EnvelopeSettings{};
    const auto oldEffects=schema>=3?effects:ReverbSettings{};
    const auto oldNoise=schema>=5?NoiseSettings{true,37}:NoiseSettings{};
    const auto oldPmon=schema>=6?pmon:PmonSettings{};
    requireMorph(state->tryStateRestoreForTest(wire),"Morph migration source rejected");state->renderMidi({on()});
    requireMorph(state->tryStateRestoreForTest(old)&&reference->tryStateRestoreForTest(
      encodeState(base,37,oldEnvelope,oldEffects,oldNoise,oldPmon)),"Old schema migration over Morph failed");
    const auto read=envelope_detail::saved(*state);
    requireMorph(!read.morph.enabled&&read.morph.amount==0&&morph_detail::same(read.definition->morphEndpoints(),{})&&
      read.definition->kind()==base->kind()&&read.definition->bankBytes()==base->bankBytes()&&read.definition->pcmCycle()==base->pcmCycle()&&
      pmon_detail::same(read.pmon,oldPmon)&&read.noise.enabled==oldNoise.enabled&&read.noise.clock==oldNoise.clock&&
      reverb_detail::same(read.reverb,oldEffects)&&envelope_detail::equal(read.envelope,oldEnvelope),"Old schema migration lost base/settings or retained Morph");
    reverb_detail::exact(*state,*reference,events,"Old schema first-block audio changed after Morph migration");
  }
  Bytes legacy(8);reverb_detail::putDouble(legacy,0,17);
  requireMorph(state->tryStateRestoreForTest(wire),"Legacy Morph source rejected");state->renderMidi({on()});
  requireMorph(state->tryStateRestoreForTest(legacy)&&reference->tryStateRestoreForTest(encodeState(builtin,17)),"Legacy migration over Morph failed");
  requireMorph(!envelope_detail::saved(*state).morph.enabled&&envelope_detail::saved(*state).morph.amount==0,"Legacy migration retained Morph");
  reverb_detail::exact(*state,*reference,events,"Legacy first-block Builtin PCM changed after Morph migration");
  for(const auto& base:{builtin,vab,pcm}) {
    const auto definition=InstrumentDefinition::withMorphEndpoints(base,endpoints);
    requireMorph(state->tryStateRestoreForTest(encodeState(definition,37,{}, {},{}, {},{true,.375})),"Retained Morph base fixture rejected");
    state->renderMidi({on()});state->plainParameterForTest(24,0);
    requireMorph(reference->tryStateRestoreForTest(encodeState(base,37)),"Original Morph-off source reference rejected");
    reverb_detail::exact(*state,*reference,events,"Morph-off did not resume exact original Builtin/VAB/PCM source");
  }
  std::cout<<"PASS "<<host<<" Morph sampleRate="<<rate
    <<" full26Params=true immutableAB=true firstRestoreBlock=true amountLive=true heldReleaseNoKON=true"
    <<" AMidB=true PMONCombined=true noisePriority=true topologyPanic=true freshCarriedControllers=true"
    <<" malformedPendingActiveAtomic=true schema1To6Legacy=true originalSourceRestored=true\n";
}
} // namespace tetorica::test
