// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Original synthetic RIFF -> pure conversion -> actual AU/VST3 state and PCM.
#pragma once
#include "web-instrument-checks.h"
#include "WavImport.h"
#include "AdpcmPlayback.h"
#include <array>
#include <cstring>

namespace tetorica::test {
namespace wav_detail {
using Bytes=std::vector<std::uint8_t>;
inline void u16(Bytes& bytes,std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));bytes.push_back(static_cast<std::uint8_t>(value>>8));
}
inline void u32(Bytes& bytes,std::uint32_t value) {
  for(unsigned n=0;n<4;++n)bytes.push_back(static_cast<std::uint8_t>(value>>(8*n)));
}
inline void tag(Bytes& bytes,const char* value){bytes.insert(bytes.end(),value,value+4);}
// This deliberately does not use the importer's fixture writer or encoder.
// At 44100 Hz a 168-frame period is exactly 262.5 Hz, close to C4. The default
// root is a tuning reference: note60 must play the recorded rate unchanged.
inline Bytes wav(unsigned rate=44100,unsigned channels=1,bool floating=false) {
  const std::uint32_t frames=rate*2/25; // 80 ms; 21 whole periods at 44100.
  const auto width=floating?4u:2u;Bytes result;
  tag(result,"RIFF");u32(result,36+frames*channels*width);tag(result,"WAVE");
  tag(result,"fmt ");u32(result,16);u16(result,floating?3:1);u16(result,static_cast<std::uint16_t>(channels));
  u32(result,rate);u32(result,rate*channels*width);u16(result,static_cast<std::uint16_t>(channels*width));
  u16(result,static_cast<std::uint16_t>(8*width));tag(result,"data");u32(result,frames*channels*width);
  constexpr double pi=3.14159265358979323846;
  for(std::uint32_t n=0;n<frames;++n)for(unsigned channel=0;channel<channels;++channel) {
    const double amplitude=channels==1?.4:(channel==0?.6:.2);
    const double sample=amplitude*std::sin(2*pi*262.5*n/rate);
    if(floating){const float value=static_cast<float>(sample);std::uint32_t bits;std::memcpy(&bits,&value,4);u32(result,bits);}
    else u16(result,static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(sample*32767.))));
  }
  return result;
}
inline WavImportResult imported(const Bytes& bytes,WavImportOptions options={}) {
  return importWav({bytes.data(),bytes.size()},"Original WAV 音色",options);
}
inline SavedInstrumentState state(const RawAdpcmSample& sample) {
  SavedInstrumentState result;
  result.definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromAdpcmSample(sample),
    {makePcmCycle(WaveformShape::Triangle),makePcmCycle(WaveformShape::Square)});
  result.gainPercent=37;result.envelope={{0xa937,0xd7bb},false,false};
  result.reverb={ReverbPreset::Room,false,true,.375,.625};result.noise={false,37};
  result.pmon={false,WaveformShape::Saw,2.5,.375,1250};result.morph={false,.375};
  return result;
}
}

template<class Factory> void checkWavImports(Factory make,double rate,const char* host) {
  using namespace wav_detail;
  const auto requireWav=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  const std::vector<Midi> first{on(0,60,37)};
  // Each format goes through actual setState/getState and starts sounding in
  // the first process block. Only owned ADPCM goes into the saved project.
  for(const auto format:std::array<std::array<unsigned,3>,4>{{
      {{44100,1,0}},{{22050,2,0}},{{48000,1,1}},{{44100,2,1}}}}) {
    auto input=wav(format[0],format[1],format[2]!=0);const auto unchanged=input;
    auto converted=imported(input);requireWav(input==unchanged,"WAV conversion modified caller bytes");
    requireWav(converted.sampleRate==format[0]&&converted.channels==format[1]&&
      converted.sourceFrames==format[0]*2/25&&!converted.sample.loopStartSamples&&
      converted.sample.name=="Original WAV 音色"&&!converted.sample.waveform,
      "WAV conversion lost source metadata or default one-shot mode");
    const auto authored=state(converted.sample);const auto encoded=web_detail::wire(authored);
    input.clear();input.shrink_to_fit(); // No source WAV/path remains for restore.
    converted.sample.adpcm.clear();
    auto a=make(),b=make();
    requireWav(a->tryStateRestoreForTest(encoded)&&b->tryStateRestoreForTest(encoded),
      "Actual bundle rejected converted WAV source");
    requireWav(a->stateBodyForTest()==encoded,"WAV saved source or one of26 parameters changed");
    const auto saved=envelope_detail::saved(*a);
    requireWav(saved.definition->kind()==InstrumentDefinition::Kind::AdpcmSample&&
      web_detail::same(saved.definition->rawAdpcmSample(),authored.definition->rawAdpcmSample()),
      "WAV project did not embed original converted sample metadata");
    reverb_detail::exact(*a,*b,first,"WAV immediate first post-restore PCM differs without original file");
    requireWav(std::all_of(a->right().begin(),a->right().begin()+37,[](float value){return value==0;})&&
      maximum(a->right())>1e-6,"WAV first-block MIDI offset or audible PCM failed");
    for(int n=0;n<8;++n)reverb_detail::exact(*a,*b,{},"WAV held PCM differs after project restore");
  }

  const auto input=wav();const auto shot=imported(input),loop=imported(input,{60,true});
  const auto octave=imported(input,{72,true});
  requireWav(loop.sample.loopStartSamples==0&&loop.sample.baseFrequencyHz>261.6&&
    loop.sample.baseFrequencyHz<261.7&&octave.sample.baseFrequencyHz==2*loop.sample.baseFrequencyHz,
    "WAV root note or explicit loop metadata is wrong");
  // The identical recording rooted one octave higher, played one octave
  // higher, must have exactly the same PITCH, phase and PCM on both channels.
  auto c4=make(),c5=make();
  requireWav(c4->tryStateRestoreForTest(web_detail::wire(state(loop.sample)))&&
    c5->tryStateRestoreForTest(web_detail::wire(state(octave.sample))),"WAV root oracle state rejected");
  requireWav(c4->renderMidi(first)==c5->renderMidi({on(0,72,37)})&&c4->right()==c5->right(),
    "WAV root-note/octave keyboard tuning differs");
  for(int n=0;n<16;++n)reverb_detail::exact(*c4,*c5,{},"WAV octave-equivalent loop PCM diverged");
  const auto periodic=blocks(*c4,static_cast<int>(std::ceil(rate*.2/256)));
  requireWav(maximum(periodic)>1e-4&&std::abs(frequency(periodic,rate)-262.5)<1.,
    "WAV loop stopped early or changed the recorded pitch at its root note");

  // The editor's metadata-only edits must reach the same real instrument as
  // importing with those choices, without decoding/requantizing the sample.
  const auto editedLoop=withAdpcmPlayback(shot.sample,true);
  const auto editedShot=withAdpcmPlayback(loop.sample,false);
  const auto editedRoot=withAdpcmRoot(loop.sample,72);
  requireWav(web_detail::same(editedLoop,loop.sample)&&web_detail::same(editedShot,shot.sample)&&
    editedRoot.adpcm==loop.sample.adpcm&&editedRoot.name==loop.sample.name&&
    std::abs(editedRoot.baseFrequencyHz-octave.sample.baseFrequencyHz)<1e-10,
    "WAV playback/root edit changed encoded sample nibbles or original metadata");
  auto edited=make(),expectedEdit=make();
  requireWav(edited->tryStateRestoreForTest(web_detail::wire(state(editedRoot)))&&
    expectedEdit->tryStateRestoreForTest(web_detail::wire(state(octave.sample))),"WAV metadata-edited state rejected");
  reverb_detail::exact(*edited,*expectedEdit,{on(0,72,37)},"WAV Root edit changed immediate restored PCM");
  for(int n=0;n<8;++n)reverb_detail::exact(*edited,*expectedEdit,{},"WAV Root edit changed held loop PCM");

  auto oneShot=make(),looping=make();
  requireWav(oneShot->tryStateRestoreForTest(web_detail::wire(state(shot.sample)))&&
    looping->tryStateRestoreForTest(web_detail::wire(state(loop.sample))),"WAV playback mode state rejected");
  oneShot->renderMidi({cc(64,127),on(),off(0,60,64)});looping->renderMidi({cc(64,127),on(),off(0,60,64)});
  const int finish=static_cast<int>(std::ceil(rate*.3/256));blocks(*oneShot,finish);blocks(*looping,finish);
  requireWav(maximum(blocks(*oneShot,8))==0&&maximum(blocks(*looping,8))>1e-4,
    "WAV One-shot did not end under Sustain or Loop did not continue");
  looping->renderMidi({cc(120,0)});requireWav(maximum(looping->renderMidi({}))==0,
    "WAV looping voice ignored All sound off");

  // A recording shorter than one SPU packet needs padding and has its loop
  // marker and END/repeat on the same packet (flag7), rather than flag4 alone.
  auto shortInput=input;shortInput.resize(44+7*2);
  envelope_detail::word(shortInput,4,static_cast<std::uint32_t>(shortInput.size()-8));
  envelope_detail::word(shortInput,40,14);
  const auto shortLoop=imported(shortInput,{60,true});
  requireWav(shortLoop.paddedFrames==21&&shortLoop.sample.adpcm.size()==16&&shortLoop.sample.adpcm[1]==7,
    "Short WAV did not retain explicit padding and a one-packet loop");
  auto onePacket=make();requireWav(onePacket->tryStateRestoreForTest(web_detail::wire(state(shortLoop.sample))),
    "Actual bundle rejected one-packet WAV loop");
  onePacket->renderMidi(first);requireWav(maximum(blocks(*onePacket,16))>1e-5,
    "One-packet WAV loop stopped at its first END packet");

  const auto authored=state(loop.sample);const auto valid=web_detail::wire(authored);
  auto actual=make(),reference=make();
  requireWav(actual->tryStateRestoreForTest(valid)&&reference->tryStateRestoreForTest(valid),
    "WAV atomic-preservation fixture rejected");
  std::vector<Bytes> malformed;
  {auto bad=input;bad.resize(43);malformed.push_back(std::move(bad));}
  {auto bad=input;envelope_detail::word(bad,24,0);malformed.push_back(std::move(bad));}
  {auto bad=input;bad[20]=2;malformed.push_back(std::move(bad));}
  {auto bad=input;bad.pop_back();malformed.push_back(std::move(bad));}
  const auto tryImport=[&](const Bytes& bytes){try{return actual->tryStateRestoreForTest(
    web_detail::wire(state(imported(bytes).sample)));}catch(const std::exception&){return false;}};
  for(const auto& bad:malformed)requireWav(!tryImport(bad)&&actual->stateBodyForTest()==valid,
    "Rejected WAV input replaced a prepared source or any of26 parameters");
  auto badState=valid;badState.at(96)=0xf0; // Raw payload's first ADPCM filter.
  requireWav(!actual->tryStateRestoreForTest(badState)&&actual->stateBodyForTest()==valid,
    "Invalid converted ADPCM cancelled a valid pending WAV state");
  const std::vector<Midi> carried{on(0,60,37),on(15,67,600),cc(64,127,15,600)};
  reverb_detail::exact(*actual,*reference,carried,"Rejected WAV changed first pending source adoption");
  for(const auto& bad:malformed) {
    requireWav(!tryImport(bad)&&actual->stateBodyForTest()==valid,"Rejected WAV changed active source/settings");
    reverb_detail::exact(*actual,*reference,{},"Rejected WAV disturbed sounding PCM or carried MIDI");
  }
  requireWav(!actual->tryStateRestoreForTest(badState)&&actual->stateBodyForTest()==valid,
    "Invalid converted ADPCM changed active source/settings");
  reverb_detail::exact(*actual,*reference,{},"Rejected converted ADPCM disturbed active PCM");
  auto isolated=make(),pristine=make();reverb_detail::exact(*isolated,*pristine,first,
    "WAV imported source leaked into another plugin instance");
  std::cout<<"PASS "<<host<<" WavImport sampleRate="<<rate
    <<" formats4=true embeddedSourceWithoutWav=true full26Params=true firstRestoreBlock=true"
    <<" rootTuning=true metadataEdits=true recordedPitch=true oneShotEndsUnderSustain=true loopContinues=true singlePacketLoop=true"
    <<" invalidPendingActiveAtomic=true carriedMidi=true separateInstance=true\n";
}
} // namespace tetorica::test
