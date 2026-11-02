// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "wav-import-checks.h"
#include "SoftwarePcm.h"

namespace tetorica::test {
namespace software_detail {
inline SoftwarePcmSample sample(std::size_t frames=4096,bool loop=true) {
  std::vector<std::int16_t> values(frames);
  for(std::size_t i=0;i<frames;++i)values[i]=static_cast<std::int16_t>(12000*std::sin(i*6.283185307179586/128));
  SoftwarePcmSample result;result.pcm=SoftwarePcmBuffer::copyOf(values);result.loop=loop;
  result.name="Original Software audio";result.reverb=true;return result;
}
inline SavedInstrumentState state(const SoftwarePcmSample& sample) {
  SavedInstrumentState result;result.gainPercent=37;result.gainBoost=1.75;result.envelope={{0xa937,0xd7bb},false,false};
  result.reverb={ReverbPreset::Room,false,true,.375,.625};result.noise={false,37};
  result.pmon={false,WaveformShape::Saw,2.5,.375,1250};result.morph={false,.375};
  result.definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromSoftwarePcmSample(sample),
    {makePcmCycle(WaveformShape::Triangle),makePcmCycle(WaveformShape::Square)});
  return result;
}
}
template<class Factory> void checkSoftwareAudio(Factory make,double rate,const char* host,
                                               const std::vector<std::uint8_t>& bank) {
  using namespace software_detail;
  const auto requireSoftware=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  // The original tiny fixture was written by the real Web version2 writer.
  auto imported=web_detail::fixture("v2-long-audio.spu.json");
  requireSoftware(imported.state.definition->kind()==InstrumentDefinition::Kind::SoftwarePcm&&
    imported.state.definition->softwarePcmSample().pcm->samples()==std::vector<std::int16_t>({0,32767,-32768,123}),
    "Actual Web version2 PCM16LE fixture was not imported exactly");
  {auto a=make(),b=make();const auto wire=web_detail::wire(imported.state);
    requireSoftware(a->tryStateRestoreForTest(wire)&&b->tryStateRestoreForTest(wire),"Actual bundle rejected Web version2 source");
    reverb_detail::exact(*a,*b,{on(0,60,37)},"Web version2 immediate PCM differed after restore");
    requireSoftware(a->stateBodyForTest()==wire,"Web version2 embedded PCM/metadata changed on save");}

  const auto source=sample();const auto authored=state(source);const auto valid=web_detail::wire(authored);
  auto a=make(),reference=make();
  requireSoftware(a->tryStateRestoreForTest(valid)&&reference->tryStateRestoreForTest(valid)&&a->stateBodyForTest()==valid,
    "Software source or full27 authored settings failed state round trip");
  reverb_detail::exact(*a,*reference,{on(0,60,37)},"Software first restore block differs");
  requireSoftware(std::all_of(a->right().begin(),a->right().begin()+37,[](float x){return x==0;})&&maximum(a->right())>1e-5,
    "Software first NoteOn offset/PCM failed");
  for(int n=0;n<8;++n)reverb_detail::exact(*a,*reference,{},"Software held source diverged");
  // Morph is a dormant authored setting for a complete recording. Host
  // automation of Enabled/Amount must not switch banks or restart its cursor.
  a->plainParameterForTest(24,1);a->plainParameterForTest(25,.875);
  reverb_detail::exact(*a,*reference,{},"Dormant Morph automation restarted/changed Software audio");
  auto captured=envelope_detail::saved(*a);
  requireSoftware(captured.morph.enabled&&captured.morph.amount==.875&&
    captured.definition->softwarePcmSample().pcm->samples()==source.pcm->samples(),
    "Software source lost raw Morph settings or PCM during automation/save");
  // A raw Morph flag cannot change this source's effective topology. A note
  // release carried from exactly the previous block end must therefore survive
  // both Enabled directions; dropping it leaves an indefinitely held loop.
  for(bool initial:{false,true}) {
    auto heldState=authored;heldState.morph.enabled=initial;
    auto carried=make(),unchanged=make();const auto wire=web_detail::wire(heldState);
    requireSoftware(carried->tryStateRestoreForTest(wire)&&unchanged->tryStateRestoreForTest(wire),
      "Software carried-off fixture rejected");
    reverb_detail::exact(*carried,*unchanged,{on()},"Software carried-off warmup diverged");
    reverb_detail::exact(*carried,*unchanged,{off(0,60,256)},"Block-end Software NoteOff applied early");
    carried->plainParameterForTest(24,initial?0:1);
    reverb_detail::exact(*carried,*unchanged,{},"Dormant Morph toggle dropped carried Software NoteOff");
    for(int n=0;n<8;++n)reverb_detail::exact(*carried,*unchanged,{},"Software release diverged after dormant Morph toggle");
    requireSoftware(maximum(carried->renderMidi({}))==0&&maximum(unchanged->renderMidi({}))==0,
      "Dormant Morph toggle left a stuck Software loop");
  }

  auto higher=source;higher.baseFrequencyHz*=2;
  {auto low=make(),high=make();requireSoftware(low->tryStateRestoreForTest(valid)&&
      high->tryStateRestoreForTest(web_detail::wire(state(higher))),"Software root edit rejected");
    requireSoftware(low->renderMidi({on(0,60,37)})==high->renderMidi({on(0,72,37)})&&low->right()==high->right(),
      "Software recording root/octave did not preserve pitch and phase");}
  {auto pairedState=authored;pairedState.pmon.enabled=true;pairedState.pmon.depth=0;
    auto dry=make(),paired=make();requireSoftware(dry->tryStateRestoreForTest(valid)&&
      paired->tryStateRestoreForTest(web_detail::wire(pairedState)),"Software paired PMON state rejected");
    reverb_detail::exact(*dry,*paired,{on(0,60,37)},"Depth-zero PMON changed Software carrier PCM");
    for(int n=0;n<8;++n)reverb_detail::exact(*dry,*paired,{},"Software PMON carrier loop diverged");}
  {auto wetState=authored;wetState.reverb={ReverbPreset::Room,true,true,1,1};
    auto dry=make(),wet=make();requireSoftware(dry->tryStateRestoreForTest(valid)&&
      wet->tryStateRestoreForTest(web_detail::wire(wetState)),"Software Reverb state rejected");
    dry->renderMidi({on()});wet->renderMidi({on()});
    const auto count=static_cast<int>(std::ceil(rate*.25/256));
    requireSoftware(delta(blocks(*dry,count),blocks(*wet,count))>1e-5,"Software carrier did not reach SPU Reverb");}
  {auto one=source;one.loop=false;
    auto shot=make(),loop=make();requireSoftware(shot->tryStateRestoreForTest(web_detail::wire(state(one)))&&
      loop->tryStateRestoreForTest(valid),"Software One-shot/Loop state rejected");
    shot->renderMidi({cc(64,127),on(),off(0,60,64)});loop->renderMidi({cc(64,127),on(),off(0,60,64)});
    const auto count=static_cast<int>(std::ceil(rate*.2/256));blocks(*shot,count);blocks(*loop,count);
    requireSoftware(maximum(blocks(*shot,8))==0&&maximum(blocks(*loop,8))>1e-4,
      "Software One-shot did not end or Loop did not continue under Sustain");}

  // Rejected state must preserve pending and active source/parameters, as well
  // as the actual stream's held/future notes and pedal events.
  a=make();reference=make();requireSoftware(a->tryStateRestoreForTest(valid)&&reference->tryStateRestoreForTest(valid),
    "Software rejection fixtures failed");
  std::vector<web_detail::Bytes> badStates;
  for(auto change:std::vector<std::pair<std::size_t,std::uint32_t>>{{8,kStateSchemaVersion+1},{32,2},{36,4},
      {40,0},{40,static_cast<std::uint32_t>(kMaxSoftwarePcmFrames+1)},{44,kMaxSoftwarePcmNameBytes+1},
      {80,0x4000},{84,1}}){auto bad=valid;envelope_detail::word(bad,change.first,change.second);badStates.push_back(std::move(bad));}
  {auto bad=valid;reverb_detail::putDouble(bad,48,0);badStates.push_back(std::move(bad));}
  {auto bad=valid;bad.pop_back();badStates.push_back(std::move(bad));}
  for(const auto& bad:badStates)requireSoftware(!a->tryStateRestoreForTest(bad)&&a->stateBodyForTest()==valid,
    "Malformed Software state changed valid pending source/27params");
  reverb_detail::exact(*a,*reference,{on(0,60,37),on(15,67,600),cc(64,127,15,600)},
    "Invalid Software state changed first-block adoption or MIDI");
  for(const auto& bad:badStates){requireSoftware(!a->tryStateRestoreForTest(bad)&&a->stateBodyForTest()==valid,
      "Malformed Software state changed active source/27params");
    reverb_detail::exact(*a,*reference,{},"Invalid Software state interrupted held/future PCM");}
  for(auto oldSource:{InstrumentDefinition::builtin(),InstrumentDefinition::fromVab(bank,3,1),
      InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle)),
      web_detail::fixture("v1-arbitrary.spu.json").state.definition}) {
    auto previous=authored;previous.definition=oldSource;previous.gainBoost=1;
    auto old=web_detail::wire(previous);old.resize(old.size()-8);
    envelope_detail::word(old,8,8);envelope_detail::word(old,12,static_cast<std::uint32_t>(old.size()));
    requireSoftware(a->tryStateRestoreForTest(valid)&&a->tryStateRestoreForTest(old)&&
      reference->tryStateRestoreForTest(web_detail::wire(previous)),"Schema8 restore over Software source rejected");
    reverb_detail::exact(*a,*reference,{on(0,60,37)},"Schema8 migration over Software source changed first PCM");}

  // One20MiB generated input per host (48kHz) proves large real host framing,
  // rather than only increasing a parser constant. Other rates use the same
  // source path above without multiplying memory-heavy fixture copies.
  if(rate==48000) {
    constexpr std::size_t frames=(20u*1024u*1024u-44)/2;
    auto wav=wav_detail::wav();wav.resize(44+frames*2);
    envelope_detail::word(wav,4,static_cast<std::uint32_t>(wav.size()-8));envelope_detail::word(wav,40,frames*2);
    for(std::size_t n=0;n<frames;++n){const auto value=static_cast<std::uint16_t>(static_cast<std::int16_t>((n%257)*128-16384));
      wav[44+n*2]=value&255;wav[45+n*2]=value>>8;}
    auto converted=importWavAudio({wav.data(),wav.size()},"20MiB Original Software WAV");
    requireSoftware(converted.softwareAudio(),"20MiB WAV did not choose Software source");
    auto largeState=state(std::get<SoftwarePcmSample>(converted.sample));const auto large=web_detail::wire(largeState);
    wav.clear();wav.shrink_to_fit();converted={};largeState={};
    auto first=make(),second=make();requireSoftware(first->tryStateRestoreForTest(large)&&
      second->tryStateRestoreForTest(large)&&first->stateBodyForTest()==large,"20MiB real host state framing/roundtrip failed");
    reverb_detail::exact(*first,*second,{on(0,60,37)},"20MiB first restore PCM requires original WAV or differs");
    requireSoftware(maximum(first->right())>1e-5,"20MiB Software source produced no first-block PCM");
  }
  std::cout<<"PASS "<<host<<" SoftwareAudio sampleRate="<<rate
    <<" webV2=true full27Params=true firstRestoreBlock=true rawMorphDormant=true dormantMorphCarriedOff=true rootTuning=true"
    <<" PMONCarrier=true reverb=true oneShotLoop=true malformedPendingActiveAtomic=true carriedMidi=true"
    <<" schema8Migration=true large20MiB="<<(rate==48000?"true":"coveredAt48k")<<"\n";
}
} // namespace tetorica::test
