// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Shared real AU/VST3 state, parameter and sample-offset MIDI/PCM assertions.
#pragma once
#include "noise-checks.h"

namespace tetorica::test {
namespace pmon_detail {
inline bool same(PmonSettings a,PmonSettings b) {
  return a.enabled==b.enabled&&a.waveform==b.waveform&&a.ratio==b.ratio&&
    a.depth==b.depth&&a.decayMs==b.decayMs;
}
template<class Instance> void author(Instance& instance,PmonSettings settings) {
  instance.plainParameterForTest(19,settings.enabled?1:0);
  instance.plainParameterForTest(20,static_cast<unsigned>(settings.waveform));
  instance.plainParameterForTest(21,settings.ratio);
  instance.plainParameterForTest(22,settings.depth);
  instance.plainParameterForTest(23,settings.decayMs);
}
}

template<class Factory> void checkPmon(Factory make,double rate,const char* host,
                                     const std::vector<std::uint8_t>& bank) {
  using Bytes=std::vector<std::uint8_t>;
  const auto requirePmon=[](bool value,const char* message) {if(!value)throw std::runtime_error(message);};
  const auto builtin=InstrumentDefinition::builtin();
  const auto pcm=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  const auto vab=InstrumentDefinition::fromVab(bank,3,1);
  const EnvelopeSettings envelope{{0xa937,0xd7bb},false,false};
  const ReverbSettings effects{ReverbPreset::Room,false,false,.375,.625};
  const NoiseSettings noise{false,37};
  const PmonSettings authored{true,WaveformShape::Saw,2.5,.375,1250};
  const PmonSettings neutral{true,WaveformShape::Sine,1,0,0};
  const auto events=std::vector<Midi>{on(0,60,37)};
  const auto wire=encodeState(pcm,37,envelope,effects,noise,authored);
  auto state=make(),reference=make();
  requirePmon(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"PMON state restore failed");
  requirePmon(state->stateBodyForTest()==wire,"PMON restore changed the full twenty-four-parameter tuple");
  reverb_detail::exact(*state,*reference,events,"PMON settings were not applied to the first post-restore NoteOn");
  requirePmon(maximum(state->right())>1e-4,"PMON first restored NoteOn produced no useful PCM");
  for(int i=0;i<8;++i)reverb_detail::exact(*state,*reference,{},"Restored PMON PCM diverged");
  const auto decoded=envelope_detail::saved(*state);
  requirePmon(decoded.gainPercent==37&&envelope_detail::equal(decoded.envelope,envelope)&&
    reverb_detail::same(decoded.reverb,effects)&&decoded.noise.clock==37&&!decoded.noise.enabled&&
    pmon_detail::same(decoded.pmon,authored)&&decoded.definition->pcmCycle()==pcm->pcmCycle(),
    "PMON/source/ADSR/Reverb/Noise settings did not round-trip independently");
  auto automated=make();
  requirePmon(automated->tryStateRestoreForTest(encodeState(pcm,37,envelope,effects,noise)),"PMON automation source rejected");
  pmon_detail::author(*automated,authored);automated->renderMidi({});
  requirePmon(automated->stateBodyForTest()==wire,"PMON parameter automation did not save all five fields");

  // A silent modulator is an independent oracle for the unchanged source.
  // Editing its shape/ratio/decay must never KON/restart a held carrier. Decay
  // changes affect future keys only, even when the modulator is already live.
  auto held=make(),plain=make();
  requirePmon(held->tryStateRestoreForTest(encodeState(builtin,100,{}, {},{},neutral)),"Neutral PMON state rejected");
  reverb_detail::exact(*held,*plain,events,"Depth-zero PMON changed the retained sample PCM");
  for(int i=0;i<8;++i)reverb_detail::exact(*held,*plain,{},"Depth-zero PMON changed held sample PCM");
  pmon_detail::author(*held,{true,WaveformShape::Square,2.5,0,1250});
  for(int i=0;i<8;++i)reverb_detail::exact(*held,*plain,{},"PMON shape/ratio/decay edit retriggered a carrier");
  held->plainParameterForTest(22,.75);
  const auto modulated=blocks(*held,16),unmodulated=blocks(*plain,16);
  requirePmon(maximum(modulated)>1e-4&&delta(modulated,unmodulated)>1e-3,
    "PMON depth automation did not affect the held carrier live");

  // Exercise each actual host parameter separately while the same key stays
  // held. Repeat tuples are exact no-ops; decay is deliberately next-key-only.
  auto live=make(),unchanged=make();
  auto liveSettings=authored;liveSettings.decayMs=0;
  const auto liveWire=encodeState(builtin,100,{}, {},{},liveSettings);
  requirePmon(live->tryStateRestoreForTest(liveWire)&&unchanged->tryStateRestoreForTest(liveWire),"Live PMON fixtures rejected");
  reverb_detail::exact(*live,*unchanged,events,"Live PMON initial PCM diverged");
  pmon_detail::author(*live,liveSettings);
  for(int i=0;i<8;++i)reverb_detail::exact(*live,*unchanged,{},"Repeated PMON settings restarted a voice");
  live->plainParameterForTest(23,13);
  for(int i=0;i<16;++i)reverb_detail::exact(*live,*unchanged,{},"PMON decay edit changed a held modulator envelope");
  for(const auto edit:{std::pair<int,double>{20,3},{21,.5},{22,.125}}) {
    live->plainParameterForTest(edit.first,edit.second);
    const auto a=blocks(*live,8),b=blocks(*unchanged,8);
    requirePmon(maximum(a)>1e-4&&delta(a,b)>1e-4,"PMON waveform/ratio/depth live automation had no audible effect");
    unchanged->plainParameterForTest(edit.first,edit.second);
    // Phases can now differ, so re-restore the newly authored tuple to compare
    // the next independent edit, without mistaking an earlier change for it.
    auto nextState=envelope_detail::saved(*live);nextState.pmon.decayMs=0;
    const auto next=encodeState(nextState.definition,nextState.gainPercent,nextState.envelope,
      nextState.reverb,nextState.noise,nextState.pmon);
    requirePmon(live->tryStateRestoreForTest(next)&&unchanged->tryStateRestoreForTest(next),"Live PMON authored state rejected");
    reverb_detail::exact(*live,*unchanged,events,"Re-restored live PMON first block diverged");
  }

  // Four distinct modulator shapes use the selected PCM carrier, rather than
  // replacing its sample. A zero decay holds modulation for this comparison.
  std::vector<std::vector<float>> shapes;
  for(unsigned shape=0;shape<4;++shape) {
    auto voice=make();auto setting=authored;setting.waveform=static_cast<WaveformShape>(shape);setting.decayMs=0;
    requirePmon(voice->tryStateRestoreForTest(encodeState(pcm,37,envelope,effects,noise,setting)),"PMON shape state rejected");
    voice->renderMidi(events);shapes.push_back(blocks(*voice,16));
  }
  for(std::size_t i=0;i<shapes.size();++i)for(std::size_t j=i+1;j<shapes.size();++j)
    requirePmon(delta(shapes[i],shapes[j])>1e-4,"Distinct PMON waveforms produced identical PCM");
  auto quickDecay=make(),holdDecay=make();
  requirePmon(quickDecay->tryStateRestoreForTest(liveWire)&&holdDecay->tryStateRestoreForTest(liveWire),"PMON next-key decay fixtures rejected");
  reverb_detail::exact(*quickDecay,*holdDecay,events,"PMON decay fixtures initially diverged");
  quickDecay->plainParameterForTest(23,13);
  for(int i=0;i<16;++i)reverb_detail::exact(*quickDecay,*holdDecay,{},"PMON decay edit applied before the next key");
  reverb_detail::exact(*quickDecay,*holdDecay,{cc(120,0)},"PMON next-key panic references diverged");
  quickDecay->renderMidi(events);holdDecay->renderMidi(events);
  requirePmon(delta(blocks(*quickDecay,32),blocks(*holdDecay,32))>1e-3,
    "PMON decay edit did not affect a subsequent key");

  // Raw both-On flags are legal, retained state. Noise wins without repeated
  // topology panic, including when dormant PMON parameters are automated.
  auto priority=make(),noiseOnly=make();
  const auto both=encodeState(pcm,37,envelope,effects,{true,48},authored);
  requirePmon(priority->tryStateRestoreForTest(both)&&noiseOnly->tryStateRestoreForTest(
    encodeState(pcm,37,envelope,effects,{true,48},{})),"Noise+PMON tuple rejected");
  requirePmon(priority->stateBodyForTest()==both,"Noise priority rewrote saved raw PMON enable");
  reverb_detail::exact(*priority,*noiseOnly,events,"Noise did not take priority over raw PMON enable");
  pmon_detail::author(*priority,{true,WaveformShape::Square,.5,.75,0});
  for(int i=0;i<16;++i)reverb_detail::exact(*priority,*noiseOnly,{},"Dormant PMON automation reset a held Noise voice");
  priority->plainParameterForTest(17,0);
  auto pmonReference=make();
  requirePmon(pmonReference->tryStateRestoreForTest(encodeState(pcm,37,envelope,effects,{false,48},
    {true,WaveformShape::Square,.5,.75,0})),"Noise-to-PMON reference rejected");
  reverb_detail::exact(*priority,*pmonReference,events,"Noise-off did not activate retained PMON on the fresh NoteOn");

  // Topology switches clear old carried notes and held keys, but retain both
  // fresh current-block NoteOn and carried wheel/pedal input. The event at600
  // is still future after the first256-frame block, then arrives atoffset88.
  for(int destination:{0,1,2}) { // Sample, Noise, PMON.
    auto switched=make(),expected=make();
    const auto starting=destination==2?encodeState(builtin,100):encodeState(builtin,100,{}, {},{},neutral);
    requirePmon(switched->tryStateRestoreForTest(starting),"PMON topology starting source rejected");
    switched->renderMidi({on(),on(15,67,600),bend(12288,15,600),cc(64,127,15,600)});
    if(destination==0)switched->plainParameterForTest(19,0);
    if(destination==1)switched->plainParameterForTest(17,1);
    if(destination==2)pmon_detail::author(*switched,neutral);
    const auto destinationWire=destination==1?encodeState(builtin,100,{}, {},{true,48},neutral):
      encodeState(builtin,100,{}, {},{},destination==2?neutral:PmonSettings{});
    requirePmon(expected->tryStateRestoreForTest(destinationWire),"PMON topology reference rejected");
    reverb_detail::exact(*switched,*expected,{},"Source topology retained an old held voice");
    const auto freshEvents=std::vector<Midi>{on(15,60,37),off(15,60,200)};
    const auto expectedEvents=std::vector<Midi>{on(15,60,37),bend(12288,15,88),cc(64,127,15,88),off(15,60,200)};
    const auto actual=switched->renderMidi(freshEvents),wanted=expected->renderMidi(expectedEvents);
    requirePmon(actual==wanted&&switched->right()==expected->right(),
      "Source topology lost carried wheel/pedal or a fresh NoteOn, or replayed an old carried note");
    for(int i=0;i<16;++i)reverb_detail::exact(*switched,*expected,{},"Topology controller/pedal carry diverged");
    requirePmon(maximum(switched->right())>1e-4,"Carried sustain pedal failed to hold a fresh key");
    reverb_detail::exact(*switched,*expected,{cc(64,0,15,31)},"Topology pedal-up release diverged");
  }

  // Reject the complete invalid tuple before mutating active or pending
  // sources, MIDI runtime, any of the24 parameters, or the live waveform.
  requirePmon(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"PMON atomic fixtures rejected");
  std::vector<Bytes> invalid;
  for(auto flags:{2u,0xffffffffu}) {auto bad=wire;envelope_detail::word(bad,80+448,flags);invalid.push_back(bad);}
  for(auto shape:{4u,0xffffffffu}) {auto bad=wire;envelope_detail::word(bad,84+448,shape);invalid.push_back(bad);}
  for(auto offset:{88u,96u,104u})for(auto value:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    auto bad=wire;reverb_detail::putDouble(bad,offset+448,value);invalid.push_back(bad);
  }
  for(const auto field:{std::pair<unsigned,double>{88,.009},{88,16.01},{96,1.01},{104,5001}}) {
    auto bad=wire;reverb_detail::putDouble(bad,field.first+448,field.second);invalid.push_back(bad);
  }
  auto bad=wire;bad.pop_back();invalid.push_back(bad);
  bad=wire;envelope_detail::word(bad,8,8);invalid.push_back(bad);
  for(const auto& bytes:invalid)requirePmon(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,
    "Malformed PMON state changed pending source/parameters");
  reverb_detail::exact(*state,*reference,events,"Malformed PMON state altered the first restored key");
  for(const auto& bytes:invalid) {
    requirePmon(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,"Malformed PMON state changed active source/parameters");
    reverb_detail::exact(*state,*reference,{},"Malformed PMON state interrupted held audio");
  }

  // Prior formats reset PMON to its disabled defaults, preserving every
  // source/ADSR/FX/Noise field that that format actually contained.
  for(unsigned schema=1;schema<=5;++schema) {
    const auto source=schema>=4?pcm:vab;
    const auto size=schema>=4?448:bank.size();
    auto old=encodeState(source,37,envelope,effects,{true,37},authored);
    old.resize(size+(schema==1?40:schema==2?48:schema<=4?72:80));
    envelope_detail::word(old,8,schema);envelope_detail::word(old,12,static_cast<std::uint32_t>(old.size()));
    const auto oldEnvelope=schema>=2?envelope:EnvelopeSettings{};
    const auto oldEffects=schema>=3?effects:ReverbSettings{};
    const auto oldNoise=schema>=5?NoiseSettings{true,37}:NoiseSettings{};
    requirePmon(state->tryStateRestoreForTest(wire),"PMON migration source rejected");state->renderMidi({on()});
    requirePmon(state->tryStateRestoreForTest(old)&&reference->tryStateRestoreForTest(
      encodeState(source,37,oldEnvelope,oldEffects,oldNoise)),"Old schema migration over PMON failed");
    const auto read=envelope_detail::saved(*state);
    requirePmon(pmon_detail::same(read.pmon,{})&&read.definition->kind()==source->kind()&&
      read.definition->pcmCycle()==source->pcmCycle()&&read.definition->bankBytes()==source->bankBytes()&&
      envelope_detail::equal(read.envelope,oldEnvelope)&&reverb_detail::same(read.reverb,oldEffects)&&
      read.noise.enabled==oldNoise.enabled&&read.noise.clock==oldNoise.clock,"Old schema migration lost source/settings or retained PMON");
    reverb_detail::exact(*state,*reference,events,"Old schema migration changed the first post-restore audio");
  }
  Bytes legacy(8);reverb_detail::putDouble(legacy,0,17);
  requirePmon(state->tryStateRestoreForTest(wire),"Legacy PMON fixture rejected");state->renderMidi({on()});
  requirePmon(state->tryStateRestoreForTest(legacy)&&reference->tryStateRestoreForTest(encodeState(builtin,17)),"Legacy migration over PMON failed");
  requirePmon(pmon_detail::same(envelope_detail::saved(*state).pmon,{}),"Legacy migration retained PMON");
  reverb_detail::exact(*state,*reference,events,"Legacy migration changed Builtin audio");
  for(const auto& source:{builtin,vab}) {
    const auto sourceWire=encodeState(source,37,envelope,effects,noise,authored);
    requirePmon(state->tryStateRestoreForTest(sourceWire)&&reference->tryStateRestoreForTest(sourceWire)&&
      state->stateBodyForTest()==sourceWire,"PMON discarded an independent Builtin/VAB source");
    reverb_detail::exact(*state,*reference,events,"PMON settings were not applied after Builtin/VAB engine exchange");
    requirePmon(maximum(state->right())>1e-4,"Retained Builtin/VAB PMON source did not sound");
  }

  // Existing complete bend/pedal/channel/lifecycle suite also passes through
  // the paired engine, with depth0 providing an unmodulated pitch oracle.
  checkControllers([&] {auto instance=make();requirePmon(instance->tryStateRestoreForTest(
    encodeState(builtin,100,{}, {},{},neutral)),"Paired MIDI fixture rejected");return instance;
  },rate,"PMON paired MIDI");
  std::cout<<"PASS "<<host<<" PMON sampleRate="<<rate
    <<" full24Params=true firstRestoreBlock=true depthZeroOriginal=true noRetrigger=true fourShapes=true"
    <<" waveformRatioDepthLive=true decayNextKey=true noisePriority=true bothRawFlagsSaved=true"
    <<" topologyPanic=true freshAndCarriedControllers=true malformedAtomic=true schema1To5Legacy=true pairedMIDI=true\n";
}
} // namespace tetorica::test
