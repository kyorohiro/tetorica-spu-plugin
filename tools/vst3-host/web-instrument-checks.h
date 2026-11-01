// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Web fixture -> pure importer -> real AU/VST3 state/MIDI/PCM, without an editor.
#pragma once
#include "morph-checks.h"
#include "WebInstrument.h"
#include <fstream>
#include <iterator>
#include <string>

#ifndef WEB_INSTRUMENT_FIXTURE_DIR
#define WEB_INSTRUMENT_FIXTURE_DIR VAB_FIXTURE_DIR "/../web"
#endif

namespace tetorica::test {
namespace web_detail {
using Bytes=std::vector<std::uint8_t>;
inline ImportedWebInstrument fixture(const char* name) {
  std::ifstream file(std::string(WEB_INSTRUMENT_FIXTURE_DIR)+"/"+name,std::ios::binary);
  if(!file)throw std::runtime_error("Cannot read original Web instrument fixture");
  const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
  return readWebInstrument(text);
}
inline Bytes wire(const SavedInstrumentState& s) {
  return encodeState(s.definition,s.gainPercent,s.envelope,s.reverb,s.noise,s.pmon,s.morph);
}
inline bool same(const RawAdpcmSample& a,const RawAdpcmSample& b) {
  return a.adpcm==b.adpcm&&a.baseFrequencyHz==b.baseFrequencyHz&&a.loopStartSamples==b.loopStartSamples&&
    a.volumeLeft==b.volumeLeft&&a.volumeRight==b.volumeRight&&a.envelope.adsr1==b.envelope.adsr1&&
    a.envelope.adsr2==b.envelope.adsr2&&a.reverb==b.reverb&&a.inspectorPitch==b.inspectorPitch&&
    a.name==b.name&&a.waveform==b.waveform;
}
}

template<class Factory> void checkWebInstruments(Factory make,double rate,const char* host,
                                                const std::vector<std::uint8_t>& bank) {
  using namespace web_detail;
  const auto requireWeb=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  const auto events=std::vector<Midi>{on(0,60,37)};
  const auto original=fixture("v1-arbitrary.spu.json");
  const auto& raw=original.state.definition->rawAdpcmSample();
  const Bytes expectedAdpcm{1,0,16,50,67,101,118,119,119,119,119,119,102,85,52,18,
    69,0,167,255,238,223,238,237,221,222,253,237,254,254,15,240,
    4,1,9,0,0,0,0,0,0,0,0,0,0,0,0,0};
  requireWeb(original.name=="明るい One shot"&&raw.name==original.name&&raw.adpcm==expectedAdpcm&&
    raw.baseFrequencyHz==123.5&&!raw.loopStartSamples&&raw.volumeLeft==-.2&&raw.volumeRight==.8&&
    raw.envelope.adsr1==0x8f4a&&raw.envelope.adsr2==0xe789&&raw.inspectorPitch==0x1234&&raw.reverb,
    "Actual Web arbitrary fixture metadata or original ADPCM changed");

  // Canonical and legacy Web encoders must retain their original bytes; optional
  // waveform/Morph metadata must not cause a hidden re-encode during DAW save.
  for(const auto* name:{"v1-arbitrary.spu.json","v1-defaults.spu.json","v1-waveform.spu.json",
                       "v1-waveform-legacy.spu.json","v1-morph.spu.json","v1-noise.spu.json"}) {
    auto imported=fixture(name);imported.state.gainPercent=37;
    requireWeb(imported.state.definition->kind()==InstrumentDefinition::Kind::AdpcmSample,
      "Web version1 import did not preserve a raw ADPCM definition");
    const auto encoded=wire(imported.state);auto a=make(),b=make();
    requireWeb(a->tryStateRestoreForTest(encoded)&&b->tryStateRestoreForTest(encoded),"Actual bundle rejected a valid Web instrument");
    requireWeb(a->stateBodyForTest()==encoded,"Web instrument changed bytes or parameters on DAW save");
    reverb_detail::exact(*a,*b,events,"Web instrument first post-restore NoteOn PCM differs");
    requireWeb(std::all_of(a->right().begin(),a->right().begin()+37,[](float x){return x==0;}),
      "Imported Web instrument sounded before its MIDI offset");
    requireWeb(maximum(a->right())>1e-6,"Imported Web instrument produced no first-block PCM");
    for(int n=0;n<4;++n)reverb_detail::exact(*a,*b,{},"Web imported held PCM diverged");
    const auto saved=envelope_detail::saved(*a);
    requireWeb(same(saved.definition->rawAdpcmSample(),imported.state.definition->rawAdpcmSample())&&
      saved.gainPercent==37&&morph_detail::same(saved.definition->morphEndpoints(),imported.state.definition->morphEndpoints()),
      "Web raw source/endpoints/Gain did not survive DAW state");
  }

  auto cycle=fixture("v1-waveform.spu.json").state.definition->rawAdpcmSample();
  cycle.waveform.reset();cycle.volumeLeft=.5;cycle.volumeRight=-.5;cycle.reverb=false;
  const auto cycleDefinition=InstrumentDefinition::fromAdpcmSample(cycle);
  auto phase=make();requireWeb(phase->tryStateRestoreForTest(encodeState(cycleDefinition,100)),"Signed volume fixture rejected");
  const auto left=phase->renderMidi(events);const auto& right=phase->right();
  requireWeb(maximum(left)>1e-4&&maximum(right)>1e-4,"Signed volumes muted a channel");
  double polarityError=0;
  for(std::size_t n=0;n<left.size();++n)
    polarityError=std::max(polarityError,std::abs(double(left[n])+right[n]));
  // SPU integer volume/master products round positive and negative values
  // differently by a PCM16 unit; plugin headroom scales that unit by 1/4.
  constexpr double polarityTolerance=2./(32768.*4.);
  if(polarityError>polarityTolerance)std::cerr<<"Raw polarity diagnostic: max(L+R)="<<polarityError
    <<" peaks="<<maximum(left)<<"/"<<maximum(right)<<'\n';
  requireWeb(polarityError<=polarityTolerance,"Imported negative volume did not invert channel polarity");

  // Independent pitch oracle: doubling sample root and adding an octave to the
  // MIDI key must produce exactly the same hardware pitch and PCM. Inspector
  // pitch is saved metadata, not a second multiplier on keyboard tuning.
  auto tuned=cycle;tuned.baseFrequencyHz=123.5;tuned.inspectorPitch=0;
  auto octave=tuned;octave.baseFrequencyHz*=2;octave.inspectorPitch=0x3fff;
  auto low=make(),high=make();
  requireWeb(low->tryStateRestoreForTest(encodeState(InstrumentDefinition::fromAdpcmSample(tuned),100))&&
    high->tryStateRestoreForTest(encodeState(InstrumentDefinition::fromAdpcmSample(octave),100)),"Raw tuning oracle rejected");
  requireWeb(low->renderMidi(events)==high->renderMidi({on(0,72,37)})&&low->right()==high->right(),
    "Raw root tuning or saved inspector pitch changed keyboard pitch");
  for(int n=0;n<8;++n)reverb_detail::exact(*low,*high,{},"Raw octave-equivalent held PCM diverged");

  auto sourceEnvelope=make(),customEnvelope=make();
  auto originalDry=original.state;originalDry.reverb={};originalDry.noise={};originalDry.pmon={};originalDry.morph={};
  auto override=originalDry;override.envelope={raw.envelope,true,true};override.envelope.custom.adsr2&=~0x2000u;
  requireWeb(sourceEnvelope->tryStateRestoreForTest(wire(originalDry))&&customEnvelope->tryStateRestoreForTest(wire(override)),
    "Raw source ADSR oracle rejected");
  reverb_detail::exact(*sourceEnvelope,*customEnvelope,events,"Raw original ADSR differs from equivalent Custom ADSR");

  const ReverbSettings room{ReverbPreset::Room,true,true,1,1};
  auto sends=cycle;sends.volumeRight=.5;sends.reverb=true;auto drySource=sends;drySource.reverb=false;
  auto wet=make(),gated=make(),dry=make();
  requireWeb(wet->tryStateRestoreForTest(encodeState(InstrumentDefinition::fromAdpcmSample(sends),100,{},room))&&
    gated->tryStateRestoreForTest(encodeState(InstrumentDefinition::fromAdpcmSample(drySource),100,{},room))&&
    dry->tryStateRestoreForTest(encodeState(InstrumentDefinition::fromAdpcmSample(drySource),100)),"Raw Tone-send fixtures rejected");
  wet->renderMidi(events);gated->renderMidi(events);dry->renderMidi(events);
  const int held=static_cast<int>(std::ceil(rate*.25/256));
  const auto wetPcm=reverb_detail::reverbBlocks(*wet,held),gatedPcm=reverb_detail::reverbBlocks(*gated,held),dryPcm=reverb_detail::reverbBlocks(*dry,held);
  requireWeb(gatedPcm.left==dryPcm.left&&gatedPcm.right==dryPcm.right&&delta(wetPcm.left,dryPcm.left)>1e-5,
    "Raw source reverb flag did not independently control Tone-send wet output");

  // All26 values are deliberately authored, including dormant settings. Reject
  // malformed raw data before replacing pending or active state/MIDI runtime.
  SavedInstrumentState full;full.definition=cycleDefinition;full.gainPercent=37;
  full.envelope={{0xa937,0xd7bb},false,false};full.reverb={ReverbPreset::Room,false,true,.375,.625};
  full.noise={false,37};full.pmon={false,WaveformShape::Saw,2.5,.375,1250};full.morph={false,.375};
  const auto valid=wire(full);const auto payload=valid.size()-kStateFixedBytes;
  auto active=make(),reference=make();
  requireWeb(active->tryStateRestoreForTest(valid)&&reference->tryStateRestoreForTest(valid),"Raw atomic fixtures rejected");
  std::vector<Bytes> invalid;
  const auto badWord=[&](std::size_t at,std::uint32_t value){auto bad=valid;envelope_detail::word(bad,at,value);invalid.push_back(std::move(bad));};
  badWord(8,kStateSchemaVersion+1);badWord(8,7);badWord(16,4);badWord(20,1);badWord(24,1);
  badWord(32,2);badWord(36,8);badWord(40,17);badWord(44,kMaxRawAdpcmNameBytes+1);
  badWord(72,1);badWord(80,0x4000);badWord(84,1);
  for(auto at:{48u,56u,64u})for(double value:{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),-1.01}) {
    auto bad=valid;reverb_detail::putDouble(bad,at,value);invalid.push_back(std::move(bad));
  }
  {auto bad=valid;reverb_detail::putDouble(bad,48,0);invalid.push_back(std::move(bad));}
  {auto bad=valid;reverb_detail::putDouble(bad,32+payload,std::numeric_limits<double>::quiet_NaN());invalid.push_back(std::move(bad));}
  {auto bad=valid;bad.at(96)=0xf0;invalid.push_back(std::move(bad));}
  {auto bad=valid;bad.at(96+cycle.adpcm.size()-15)=0;invalid.push_back(std::move(bad));}
  {auto bad=valid;bad.pop_back();invalid.push_back(std::move(bad));}
  for(const auto& bad:invalid)
    requireWeb(!active->tryStateRestoreForTest(bad)&&active->stateBodyForTest()==valid,
      "Invalid Web raw state changed pending source or any of26 parameters");
  const auto carried=std::vector<Midi>{on(0,60,37),on(15,67,600),cc(64,127,15,600)};
  reverb_detail::exact(*active,*reference,carried,"Malformed raw state altered first fresh key or pending engine");
  for(const auto& bad:invalid) {
    requireWeb(!active->tryStateRestoreForTest(bad)&&active->stateBodyForTest()==valid,
      "Invalid Web raw state changed active source/26 parameters");
    reverb_detail::exact(*active,*reference,{},"Rejected raw state interrupted PCM or discarded carried MIDI");
  }
  auto isolated=make(),pristine=make();
  reverb_detail::exact(*isolated,*pristine,events,"Web imported state leaked into another plugin instance");

  // The previous published schema7 already had full Morph endpoints. Loading
  // it after a raw import must restore its original source and every setting.
  const auto endpoints=original.state.definition->morphEndpoints();
  for(auto base:{InstrumentDefinition::builtin(),InstrumentDefinition::fromVab(bank,3,1),
                 InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle))}) {
    auto previous=full;previous.definition=InstrumentDefinition::withMorphEndpoints(base,endpoints);
    previous.morph={true,.375};previous.pmon.enabled=true;
    auto old=wire(previous);envelope_detail::word(old,8,7);
    requireWeb(active->tryStateRestoreForTest(valid)&&active->tryStateRestoreForTest(old)&&
      reference->tryStateRestoreForTest(wire(previous)),"Schema7 restore over raw import rejected");
    requireWeb(active->stateBodyForTest()==wire(previous),"Schema7 migration lost an original source, endpoints or parameter");
    reverb_detail::exact(*active,*reference,events,"Schema7 migration changed first source/Morph PCM");
  }
  std::cout<<"PASS "<<host<<" WebInstrument sampleRate="<<rate
    <<" actualWebFixtures6=true rawBytesMetadata=true full26Params=true firstRestoreBlock=true"
    <<" signedPolarity=true rootTuning=true sourceADSR=true sourceReverbSend=true"
    <<" malformedPendingActiveAtomic=true carriedMidi=true separateInstance=true schema7Migration=true\n";
}
} // namespace tetorica::test
