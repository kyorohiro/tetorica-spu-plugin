// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Exercise the shipped AU/VST3 MIDI and state APIs, without a native-core oracle.
#pragma once
#include "InstrumentState.h"
#include "midi-controller-checks.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tetorica::test {
namespace builtin_wide_detail {
using Bytes=std::vector<std::uint8_t>;
inline void requireWide(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}
inline void word(Bytes& bytes,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(8*i));
}
inline void number(Bytes& bytes,std::size_t at,double value) {
  std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
  for(unsigned i=0;i<8;++i)bytes.at(at+i)=static_cast<std::uint8_t>(bits>>(8*i));
}
// Original fixed schemas, built explicitly rather than encoding a new kind5
// state and relabelling it. Kind0 and the historical default fields stay fixed.
inline Bytes oldBuiltin(unsigned schema) {
  const std::size_t size=schema==1?40:schema==2?48:schema<=4?72:
    schema==5?80:schema==6?112:1024;
  Bytes bytes(size,0);std::copy(kStateMagic.begin(),kStateMagic.end(),bytes.begin());
  word(bytes,8,schema);word(bytes,12,static_cast<std::uint32_t>(size));number(bytes,32,100);
  if(schema>=2){word(bytes,40,0x1fc0000f);word(bytes,44,2);}
  if(schema>=3){number(bytes,56,.25);number(bytes,64,.25);}
  if(schema>=5)word(bytes,76,48);
  if(schema>=6){number(bytes,88,1);number(bytes,96,.5);number(bytes,104,500);}
  if(schema>=7) {
    const MorphEndpoints endpoints{};
    for(std::size_t i=0;i<112;++i) {
      std::uint32_t bits=0;std::memcpy(&bits,&endpoints.a[i],sizeof(bits));word(bytes,128+4*i,bits);
      std::memcpy(&bits,&endpoints.b[i],sizeof(bits));word(bytes,576+4*i,bits);
    }
  }
  return bytes;
}
inline unsigned factor(int note) {
  return note<90?1u:note<102?2u:note<114?4u:note<126?8u:16u;
}
// Independent register quantization: choose the source zone from the key only,
// then compute the signed-endpoint bend and round the PITCH register once.
inline double expectedHz(int note,int wheel,bool wide) {
  constexpr double root=44100./112.;
  const auto multiplier=wide?factor(note):1u;
  const double semitones=wheel<8192?double(wheel-8192)*2./8192.:
    double(wheel-8192)*2./8191.;
  const double desired=440.*std::pow(2.,(double(note)-69.+semitones)/12.);
  const double raw=4096.*desired/(root*multiplier);
  const auto pitch=std::min(16383l,std::max(1l,std::lround(raw)));
  return root*multiplier*double(pitch)/4096.;
}
// Count real positive crossings with fractional interpolation. Never clamp the
// estimator to an expected range: a legacy 1.575kHz plateau must fail wide tests.
inline double measuredHz(const std::vector<float>& audio,double rate) {
  requireWide(audio.size()>4096,"Wide Sine measurement is too short");
  std::size_t crossings=0;double first=0,last=0;
  for(std::size_t i=1025;i<audio.size();++i)if(audio[i-1]<=0&&audio[i]>0) {
    const double at=double(i)-double(audio[i])/double(audio[i]-audio[i-1]);
    if(crossings++==0)first=at;
    last=at;
  }
  requireWide(crossings>100&&last>first,"Wide Sine produced too few measured cycles");
  return double(crossings-1)*rate/(last-first);
}
inline void frequencyIs(const std::vector<float>& audio,double rate,int note,int wheel,bool wide) {
  requireWide(maximum(audio)>1e-4,"Builtin Sine is inaudible in the measured high range");
  const auto measured=measuredHz(audio,rate),expected=expectedHz(note,wheel,wide);
  if(std::abs(measured-expected)>1.5)
    throw std::runtime_error(std::string(wide?"Wide":"Legacy")+" builtin pitch mismatch: note="+
      std::to_string(note)+" bend="+std::to_string(wheel)+" expectedHz="+
      std::to_string(expected)+" measuredHz="+std::to_string(measured)+" rate="+std::to_string(rate));
}
inline double energy(const std::vector<float>& audio,std::size_t first,std::size_t count) {
  double result=0;
  for(std::size_t i=first;i<first+count;++i)result+=double(audio.at(i))*audio.at(i);
  return result;
}
template<class Instance> std::vector<float> sounding(Instance& instance,int note,int wheel) {
  instance.resetForMidiTest();
  instance.renderMidi({bend(wheel,0),on(0,note,37)});
  blocks(instance,8);
  return blocks(instance,64);
}
template<class A,class B> void exact(A& a,B& b,const std::vector<Midi>& events,
    const char* message,int frames=256) {
  const auto left=a.renderMidi(events,frames),other=b.renderMidi(events,frames);
  requireWide(left==other&&a.right()==b.right(),message);
}
template<class Instance> SavedInstrumentState saved(Instance& instance) {
  const auto bytes=instance.stateBodyForTest();return decodeState({bytes.data(),bytes.size()});
}
}

template<class Factory> void checkBuiltinWide(Factory make,double rate,const char* host) {
  using namespace builtin_wide_detail;
  auto fresh=make();
  const auto initial=fresh->stateBodyForTest();
  const auto initialState=decodeState({initial.data(),initial.size()});
  requireWide(initialState.definition->kind()==InstrumentDefinition::Kind::BuiltinSineWide&&
    initialState.gainPercent==100&&initialState.gainBoost==1,"Fresh actual bundle did not select wide builtin Sine at default Gain");
  const auto originalParameters=fresh->soundParametersForBuiltinTest();
  requireWide(originalParameters.size()==27,"Wide Sine changed the 27 public sound parameters");
  std::size_t measured=0;
  // Include all zone edges as well as the explicitly requested high keys. Each
  // zone must retain two semitones of upward bend headroom.
  for(int key:{89,90,91,92,101,102,104,113,114,116,125,126,127})
    for(int wheel:{0,8192,16383}) {
      frequencyIs(sounding(*fresh,key,wheel),rate,key,wheel,true);++measured;
    }
  requireWide(fresh->stateBodyForTest()==initial&&
    fresh->soundParametersForBuiltinTest()==originalParameters,
    "High notes or live wheel changed saved wide source/public parameters");

  for(int key:{0,60,89})for(int wheel:{0,8192,16383}) {
    auto wide=make(),old=make();
    requireWide(old->tryStateRestoreForTest(encodeState(InstrumentDefinition::builtin(),100)),
      "Low-key legacy comparison state rejected");
    exact(*wide,*old,{bend(wheel),on(0,key,37)},"Wide Sine altered a legacy low-key first NoteOn");
    for(int i=0;i<12;++i)exact(*wide,*old,{},"Wide Sine altered original low-key PCM");
  }

  // Bend on an already sounding high key must keep the source zone selected by
  // NoteOn. Full timelines also check that sample offsets survive MIDI splits.
  for(int key:{91,104,116,127}) {
    fresh->resetForMidiTest();fresh->renderMidi({on(0,key,37)});blocks(*fresh,8);
    for(int wheel:{16383,0,8192}) {
      fresh->renderMidi({bend(wheel,0,73)});
      frequencyIs(blocks(*fresh,64),rate,key,wheel,true);++measured;
    }
  }
  {
    auto whole=make(),split=make();
    exact(*whole,*split,{on(0,104,37)},"Wide held-note reference diverged");
    for(int i=0;i<12;++i)exact(*whole,*split,{},"Wide held-note warmup diverged");
    const auto a=whole->renderMidi({bend(16383,0,73),bend(0,0,173)});const auto ar=whole->right();
    auto b=split->renderMidi({},73),br=split->right();
    concat(b,split->renderMidi({bend(16383)},100));concat(br,split->right());
    concat(b,split->renderMidi({bend(0)},83));concat(br,split->right());
    requireWide(a==b&&ar==br,"High-key live bend moved with MIDI/host block partitioning");
    for(int i=0;i<8;++i)exact(*whole,*split,{},"High-key bent timeline diverged after partitioning");
  }
  {
    auto unchanged=make(),excursion=make();
    exact(*unchanged,*excursion,{on(0,104,37)},"High-key no-retrigger fixture diverged");
    for(int i=0;i<12;++i)exact(*unchanged,*excursion,{},"High-key no-retrigger warmup diverged");
    const auto a=excursion->renderMidi({bend(16383,0,73),bend(8192,0,74)});
    const auto b=unchanged->renderMidi({});
    requireWide(std::equal(a.begin(),a.begin()+73,b.begin()),"High-key bend changed PCM before its offset");
    const auto settled=74+static_cast<std::size_t>(std::ceil(rate*6./44100.))+2;
    const auto baseline=energy(b,settled,64),actual=energy(a,settled,64);
    // One host sample of changed PITCH makes a small phase excursion. A KON
    // instead repeats the Native128-frame fade and loses most of this energy.
    requireWide(baseline>1e-5&&actual>baseline*.7,
      "High-key live bend retriggered the held source/envelope");
  }

  // Keep every existing public parameter authored across a kind5 restore.
  // Dormant source-mode settings remain present rather than becoming new IDs.
  const EnvelopeSettings envelope{{0xa937,0xd7bb},false,false};
  const ReverbSettings reverb{ReverbPreset::SpaceEcho,false,true,.375,.625};
  const NoiseSettings noise{false,23};
  const PmonSettings pmon{false,WaveformShape::Square,2.25,.375,321};
  const MorphSettings morph{false,.625};
  const auto authored=encodeState(InstrumentDefinition::builtinWide(),37,envelope,reverb,noise,pmon,morph);
  auto roundtrip=make(),reference=make();
  requireWide(roundtrip->tryStateRestoreForTest(authored)&&reference->tryStateRestoreForTest(authored),
    "Actual host rejected an authored wide Sine state");
  requireWide(roundtrip->stateBodyForTest()==authored,"Wide Sine changed its exact authored state before audio");
  const auto parameters=roundtrip->soundParametersForBuiltinTest();
  const std::array<double,27> expected{{37,0,41,1,3,7,94,1,1,27,1,0,6,0,37.5,62.5,1,0,23,0,3,2.25,.375,321,0,.625,1}};
  for(std::size_t i=0;i<expected.size();++i)requireWide(std::abs(parameters[i]-expected[i])<1e-5,
    "Wide Sine changed a public parameter ID/value during restore");
  exact(*roundtrip,*reference,{on(0,116,37)},"First restored wide Sine NoteOn/offset PCM differs");
  for(int i=0;i<12;++i)exact(*roundtrip,*reference,{},"Wide Sine restored held PCM differs");
  requireWide(roundtrip->stateBodyForTest()==authored&&roundtrip->soundParametersForBuiltinTest()==parameters,
    "Wide Sine save/restore changed one of the 27 authored sound parameters");
  for(const auto& edit:{std::pair<std::size_t,std::uint32_t>{8,9},{20,1},{24,1},{28,1}}) {
    auto malformed=authored;word(malformed,edit.first,edit.second);
    requireWide(!roundtrip->tryStateRestoreForTest(malformed)&&roundtrip->stateBodyForTest()==authored&&
      roundtrip->soundParametersForBuiltinTest()==parameters,
      "Malformed wide source/schema changed its accepted source or public parameters");
    exact(*roundtrip,*reference,{},"Rejected wide source interrupted the active high note");
  }
  const auto wideSound=sounding(*roundtrip,116,16383);
  frequencyIs(wideSound,rate,116,16383,true);++measured;
  {
    auto paired=make();
    const PmonSettings neutral{true,WaveformShape::Sine,1,0,0};
    requireWide(paired->tryStateRestoreForTest(encodeState(InstrumentDefinition::builtinWide(),100,{}, {},{},neutral)),
      "High-range depth-zero PMON source rejected");
    frequencyIs(sounding(*paired,127,16383),rate,127,16383,true);++measured;
  }

  // Every historical kind0 schema still selects the original fixed ADPCM and
  // legacy PITCH ceiling, even when replacing an active wide source.
  const auto legacy=encodeState(InstrumentDefinition::builtin(),100);
  for(unsigned schema=1;schema<=10;++schema) {
    auto old=make(),canonical=make();
    const auto fixture=oldBuiltin(schema);
    requireWide(old->tryStateRestoreForTest(fixture)&&canonical->tryStateRestoreForTest(legacy),
      "A historical kind0 schema was rejected by the actual host");
    requireWide(saved(*old).definition->kind()==InstrumentDefinition::Kind::Builtin,
      "Historical kind0 state was silently upgraded to wide Sine");
    exact(*old,*canonical,{bend(16383),on(0,104,37)},"Historical kind0 first-block PCM changed");
    for(int i=0;i<8;++i)exact(*old,*canonical,{},"Historical kind0 held PCM changed");
    const auto actual=blocks(*old,64),other=blocks(*canonical,64);
    requireWide(actual==other,"Historical kind0 PCM differs from explicit legacy source");
    frequencyIs(actual,rate,104,16383,false);++measured;
    requireWide(old->tryStateRestoreForTest(authored),"Old source could not restore a saved wide source");
    frequencyIs(sounding(*old,116,8192),rate,116,8192,true);++measured;
  }
  Bytes gainOnly(8,0);number(gainOnly,0,37);
  requireWide(fresh->tryStateRestoreForTest(gainOnly),"Legacy raw Gain state was rejected over a wide source");
  requireWide(saved(*fresh).definition->kind()==InstrumentDefinition::Kind::Builtin,
    "Legacy raw Gain state selected a wide source");
  frequencyIs(sounding(*fresh,127,16383),rate,127,16383,false);++measured;
  std::cout<<"PASS "<<host<<" builtin wide Sine sampleRate="<<rate<<" pitchMeasurements="<<measured
    <<" keys92_104_116_127=true bendEndpoints2st=true zoneEdges=true heldBendNoKON=true partitionInvariant=true"
    <<" freshWide=true originalLowKeysExact=true exactStateRoundtrip=true malformedWideAtomic=true"
    <<" publicParams27Stable=true schemas1To10LegacyPCM=true rawGainLegacy=true depthZeroPmonWide=true\n";
}
} // namespace tetorica::test
