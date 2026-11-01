// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual AU/VST3 PCM checks. No audio device, editor or production test hook.
#pragma once
#include "InstrumentState.h"
#include "midi-controller-checks.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tetorica::test {
namespace gain_detail {
inline void requireGain(bool condition,const char* message) {
  if(!condition)throw std::runtime_error(message);
}

// Independent mathematical oracle: the target is sampled at a host block
// boundary, then each rendered output sample advances a fixed five-ms line.
// Re-authoring the same target must not restart that line.
struct Line {
  explicit Line(double rate):length(static_cast<int>(std::ceil(rate*.005))) {}
  void target(double value) {
    if(value==end)return;
    start=current;end=value;position=0;
  }
  double next() {
    if(position<length) {
      ++position;
      current=position==length?end:start+(end-start)*(double(position)/length);
    }
    return current;
  }
  const int length;
  int position=0;
  double start=1,current=1,end=1;
};

template<class Instance> void same(Instance& a,Instance& b,
    const std::vector<Midi>& events={},int frames=256) {
  const auto leftA=a.renderMidi(events,frames),leftB=b.renderMidi(events,frames);
  requireGain(delta(leftA,leftB)<1e-7&&delta(a.right(),b.right())<1e-7,
    "Gain test reference phases diverged");
}
template<class Instance> void warm(Instance& a,Instance& b) {
  const auto state=encodeState(InstrumentDefinition::builtin(),100);
  requireGain(a.tryStateRestoreForTest(state)&&b.tryStateRestoreForTest(state),
    "Gain warm-up state restore failed");
  same(a,b,{on(0,60,37)});
  for(int block=0;block<8;++block)same(a,b);
}
template<class Instance> double oracleBlock(Instance& edited,Instance& reference,
    Line& line,int frames,const std::vector<Midi>& events={}) {
  const auto actual=edited.renderMidi(events,frames),baseline=reference.renderMidi(events,frames);
  double differsFromHardStep=0;
  for(int n=0;n<frames;++n) {
    const double coefficient=line.next();
    const auto expectedLeft=static_cast<float>(double(baseline[n])*coefficient);
    const auto expectedRight=static_cast<float>(double(reference.right()[n])*coefficient);
    requireGain(std::abs(double(actual[n])-expectedLeft)<2e-8&&
      std::abs(double(edited.right()[n])-expectedRight)<2e-8,
      "Actual bundle Gain does not follow the five-ms per-sample PCM oracle");
    if(coefficient==0)requireGain(actual[n]==0&&edited.right()[n]==0,
      "Settled zero Gain did not produce exact stereo silence");
    differsFromHardStep=std::max(differsFromHardStep,
      std::abs(double(actual[n])-double(baseline[n])*line.end));
  }
  return differsFromHardStep;
}
template<class Instance> double span(Instance& edited,Instance& reference,
    Line& line,int total,bool repeatTarget=false) {
  const int pattern[]{1,17,83,7,256,31,109};
  int index=0;
  double difference=0;
  while(total>0) {
    const int count=std::min(total,pattern[index++%7]);
    if(repeatTarget)edited.setGainForTest(line.end);
    difference=std::max(difference,oracleBlock(edited,reference,line,count));
    total-=count;
  }
  return difference;
}
template<class Instance> void quarterFirstNote(Instance& edited,Instance& unity,
    const char* message) {
  const auto actual=edited.renderMidi({on(0,60,37)}),baseline=unity.renderMidi({on(0,60,37)});
  for(std::size_t n=0;n<actual.size();++n)requireGain(
    std::abs(double(actual[n])-double(baseline[n])*.25)<2e-8&&
    std::abs(double(edited.right()[n])-double(unity.right()[n])*.25)<2e-8,message);
  requireGain(std::all_of(actual.begin(),actual.begin()+37,[](float value){return value==0;}),
    "Gain lifecycle first NoteOn leaked before its offset");
  requireGain(maximum(actual)>1e-4,"Gain lifecycle first-block PCM was silent");
}
}

template<class Factory> void checkGainSmoothing(Factory make,double rate,const char* host) {
  using namespace gain_detail;
  {
    auto edited=make(),reference=make();warm(*edited,*reference);
    Line line(rate);
    edited->setGainForTest(0);line.target(0);
    requireGain(span(*edited,*reference,line,line.length+97,true)>1e-4,
      "Gain down-ramp test did not distinguish a hard step");
    requireGain(line.current==0,"Gain down-ramp did not reach zero");
    edited->setGainForTest(1);line.target(1);
    requireGain(span(*edited,*reference,line,line.length+97,true)>1e-4,
      "Gain up-ramp test did not distinguish a hard step");
    edited->setGainForTest(0);line.target(0);
    span(*edited,*reference,line,line.length/3);
    edited->setGainForTest(.75);line.target(.75);
    span(*edited,*reference,line,line.length+31,true);
    const auto independentState=reference->stateBodyForTest();
    requireGain(decodeState({independentState.data(),independentState.size()}).gainPercent==100,
      "Gain automation affected an independent instance");
  }
  {
    auto whole=make(),split=make();warm(*whole,*split);
    whole->setGainForTest(.25);split->setGainForTest(.25);
    for(int block=0;block<4;++block) {
      const auto actual=whole->renderMidi({bend(8192,0,37),cc(64,0,0,173)});
      const auto rightWhole=whole->right();
      auto expected=split->renderMidi({},37);auto rightSplit=split->right();
      concat(expected,split->renderMidi({bend(8192)},136));concat(rightSplit,split->right());
      concat(expected,split->renderMidi({cc(64,0)},83));concat(rightSplit,split->right());
      requireGain(delta(actual,expected)<1e-7&&delta(rightWhole,rightSplit)<1e-7,
        "MIDI render ranges or variable host blocks restarted Gain smoothing");
    }
  }
  {
    auto authored=make(),reference=make();warm(*authored,*reference);
    authored->setGainForTest(.25);Line line(rate);line.target(.25);
    span(*authored,*reference,line,33);
    const auto saved=authored->stateBodyForTest();const auto decoded=decodeState({saved.data(),saved.size()});
    requireGain(decoded.gainPercent==25,"Gain state saved the transient ramp instead of its target");
    auto restored=make(),unity=make();
    requireGain(restored->tryStateRestoreForTest(saved)&&
      unity->tryStateRestoreForTest(encodeState(InstrumentDefinition::builtin(),100)),
      "Gain first-block restore preparation failed");
    quarterFirstNote(*restored,*unity,
      "Successful restore did not snap to saved Gain before its first NoteOn");
  }
  {
    auto attempted=make(),unchanged=make();warm(*attempted,*unchanged);
    attempted->setGainForTest(0);unchanged->setGainForTest(0);same(*attempted,*unchanged,{},33);
    const auto saved=attempted->stateBodyForTest();auto invalid=saved;invalid.at(0)^=255;
    requireGain(!attempted->tryStateRestoreForTest(invalid),"Malformed Gain-ramp state was accepted");
    requireGain(attempted->stateBodyForTest()==saved,"Failed restore changed the authored Gain state");
    for(int block=0;block<8;++block)same(*attempted,*unchanged,{},37);
  }
  for(int lifecycle=0;lifecycle<3;++lifecycle) {
    auto edited=make(),unity=make();warm(*edited,*unity);
    edited->setGainForTest(.25);Line line(rate);line.target(.25);
    span(*edited,*unity,line,33); // A genuine unfinished ramp, not settled Gain.
    if(lifecycle==0) {
      edited->resetForMidiTest();unity->resetForMidiTest();
      quarterFirstNote(*edited,*unity,"Reset did not snap mid-ramp Gain before its first NoteOn");
    }
    else if(lifecycle==1) {
      edited->reactivateForMidiTest();unity->reactivateForMidiTest();
      quarterFirstNote(*edited,*unity,"Reactivation did not snap mid-ramp Gain before its first NoteOn");
    }
    else {
      edited->setBypassForGainTest(true);unity->setBypassForGainTest(true);
      const auto muted=edited->renderMidi({}),mutedUnity=unity->renderMidi({});
      requireGain(maximum(muted)==0&&maximum(edited->right())==0&&
        maximum(mutedUnity)==0&&maximum(unity->right())==0,
        "Host bypass did not mute the active Gain-ramp voice");
      edited->setBypassForGainTest(false);unity->setBypassForGainTest(false);
      quarterFirstNote(*edited,*unity,"Unbypass did not snap mid-ramp Gain before its first NoteOn");
    }
  }
  std::cout<<"PASS "<<host<<" Gain smoothing sampleRate="<<rate
    <<" linear5ms=true downUp=true midRampRetarget=true sameTargetNoRestart=true"
    <<" sampleCountAcrossBlocks=true MIDISplits=true stateSavesTarget=true"
    <<" firstRestoreSnap=true failedRestoreKeepsRamp=true separateInstances=true"
    <<" resetFirstSnap=true reactivateFirstSnap=true bypassFirstSnap=true"
    <<" automationTargetsBlockBased=true\n";
}
} // namespace tetorica::test
