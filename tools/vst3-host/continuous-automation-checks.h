// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Real AU/VST3 ingress and PCM. Oracles use host-time arithmetic or separately
// partitioned render calls, never the production automation scheduler.
#pragma once
#include "gain-smoothing-checks.h"
#include "reverb-checks.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace tetorica::test {
namespace automation_detail {
struct Point { int id; double value; int at; };
inline void requireAutomation(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
template<class Instance> void enqueue(Instance& instance, const std::vector<Point>& points) {
  for (const auto& point : points)
    instance.plainAutomationForTest(point.id, point.value, point.at);
}
template<class Instance> SavedInstrumentState saved(Instance& instance) {
  const auto wire=instance.stateBodyForTest();
  return decodeState({wire.data(),wire.size()});
}
template<class Instance> void gainOracle(Instance& edited, Instance& reference,
    double rate, const std::vector<Point>& points, int total) {
  gain_detail::Line line(rate);
  double base=100., boost=1.;
  const int pattern[]{256,113,17,83,7,31,109};
  int cursor=0, block=0;
  while (cursor<total) {
    const auto count=std::min(total-cursor,pattern[block++%7]);
    const auto actual=edited.renderMidi({},count), baseline=reference.renderMidi({},count);
    for (int n=0;n<count;++n) {
      bool changed=false;
      for (const auto& point:points) if (point.at==cursor+n) {
        if (point.id==0) base=point.value;
        else if (point.id==26) boost=point.value;
        changed=true;
      }
      if (changed) line.target(base/100.*boost);
      const auto coefficient=line.next();
      requireAutomation(std::abs(double(actual[n])-double(baseline[n])*coefficient)<3.e-8 &&
        std::abs(double(edited.right()[n])-double(reference.right()[n])*coefficient)<3.e-8,
        "Timed Gain/Boost moved before its sample offset or departed from its five-ms PCM oracle");
    }
    if (cursor==0) {
      const auto state=saved(edited);
      requireAutomation(state.gainPercent==37.5 && state.gainBoost==1.,
        "Future Gain/Boost authoring targets were not saved independently of audible timeline");
    }
    cursor+=count;
  }
}
inline std::vector<Midi> messagesBetween(const std::vector<Midi>& messages, int start, int end) {
  std::vector<Midi> result;
  for (auto message:messages) if (message.offset>=start && message.offset<end) {
    message.offset-=start; result.push_back(message);
  }
  return result;
}
template<class Instance> void partition(Instance& whole, Instance& split,
    const std::vector<Point>& points,const std::vector<Midi>& midi,int total) {
  enqueue(whole,points);
  const int pattern[]{256,113,17,83,7,31,109};
  int cursor=0,block=0;
  while(cursor<total) {
    const int end=std::min(total,cursor+pattern[block++%7]);
    const auto actual=whole.renderMidi(messagesBetween(midi,cursor,end),end-cursor);
    const auto actualRight=whole.right();
    std::vector<float> expected,expectedRight;
    for(int at=cursor;at<end;) {
      for(const auto& point:points) if(point.at==at)
        split.plainAutomationForTest(point.id,point.value,0);
      int boundary=end;
      for(const auto& point:points) if(point.at>at) boundary=std::min(boundary,point.at);
      concat(expected,split.renderMidi(messagesBetween(midi,at,boundary),boundary-at));
      concat(expectedRight,split.right());
      at=boundary;
    }
    requireAutomation(actual==expected && actualRight==expectedRight,
      "Morph/Reverb automation depends on host block partition or lost same-offset MIDI ordering");
    cursor=end;
  }
}
}

template<class Factory> void checkContinuousAutomation(Factory make,double rate,const char* host) {
  using namespace automation_detail;
  const auto builtin=InstrumentDefinition::builtin();
  {
    auto edited=make(),reference=make();
    gain_detail::warm(*edited,*reference);
    // Two values at the same offset use the last value. Future offsets cross
    // several unequal blocks; Gain and Boost at400 must form one target.
    const std::vector<Point> points{{0,25,37},{26,1.5,100},{0,75,173},{0,50,173},
      {0,100,400},{26,3.8,400},{0,0,523},{26,1,523},{0,37.5,600}};
    enqueue(*edited,points);
    gainOracle(*edited,*reference,rate,points,1100);
    const auto parameters=edited->soundParametersForBuiltinTest();
    requireAutomation(parameters.size()==27 && parameters[0]==37.5 && parameters[26]==1,
      "Gain automation changed legacy parameter zero mapping or Boost ID26");
  }
  {
    auto amplified=make(),unity=make();
    const auto wire=encodeState(builtin,100,{}, {},{}, {},{},3.8);
    requireAutomation(amplified->tryStateRestoreForTest(wire) &&
      unity->tryStateRestoreForTest(encodeState(builtin,100)),"Boosted bundle state was rejected");
    requireAutomation(amplified->stateBodyForTest()==wire && saved(*amplified).gainBoost==3.8,
      "Boost did not survive exact new-schema host state roundtrip");
    const auto actual=amplified->renderMidi({on(0,60,37)}),baseline=unity->renderMidi({on(0,60,37)});
    requireAutomation(maximum(baseline)>1.e-4,"Boosted first-note test was silent");
    for(std::size_t n=0;n<actual.size();++n)
      requireAutomation(std::abs(double(actual[n])-double(baseline[n])*3.8)<3.e-8 &&
        std::abs(double(amplified->right()[n])-double(unity->right()[n])*3.8)<3.e-8,
        "Actual bundle did not apply 380 percent Gain before its first restored note");
    const auto original=unity->soundParametersForBuiltinTest();
    requireAutomation(original[0]==100 && original[26]==1,"Gain Boost leaked into another instance");
    for(double value:{.99,3.81,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
      auto malformed=wire;reverb_detail::putDouble(malformed,malformed.size()-8,value);
      requireAutomation(!amplified->tryStateRestoreForTest(malformed) && amplified->stateBodyForTest()==wire,
        "Malformed Gain Boost changed accepted source or host parameter state");
      const auto a=amplified->renderMidi({}),b=unity->renderMidi({});
      for(std::size_t n=0;n<a.size();++n)
        requireAutomation(std::abs(double(a[n])-double(b[n])*3.8)<3.e-8 &&
          std::abs(double(amplified->right()[n])-double(unity->right()[n])*3.8)<3.e-8,
          "Rejected Gain Boost interrupted a held voice or changed its audible Gain");
    }
    // A real historical schema10 body has no trailing Boost field. Its old
    // normalized Gain must regain its original level even over a boosted state.
    auto legacy=wire;legacy.resize(legacy.size()-8);
    envelope_detail::word(legacy,8,10);envelope_detail::word(legacy,12,static_cast<std::uint32_t>(legacy.size()));
    requireAutomation(amplified->tryStateRestoreForTest(legacy) &&
      unity->tryStateRestoreForTest(encodeState(builtin,100)) && saved(*amplified).gainBoost==1,
      "Schema10 restore retained a previous Gain Boost");
    reverb_detail::exact(*amplified,*unity,{on(0,60,37)},"Legacy normalized Gain level changed after boosted state migration");
  }
  {
    const MorphEndpoints endpoints{makePcmCycle(WaveformShape::Sine),makePcmCycle(WaveformShape::Square)};
    const auto source=InstrumentDefinition::withMorphEndpoints(builtin,endpoints);
    const ReverbSettings reverb{ReverbPreset::Room,true,false,.75,.5};
    const auto wire=encodeState(source,37,{},reverb,{}, {},{true,0});
    auto whole=make(),split=make();
    requireAutomation(whole->tryStateRestoreForTest(wire) && split->tryStateRestoreForTest(wire),
      "Morph/Reverb automation fixture was rejected");
    reverb_detail::exact(*whole,*split,{on(0,60,37)},"Morph/Reverb warm-up diverged");
    for(int n=0;n<static_cast<int>(std::ceil(rate*.2/256.));++n)
      reverb_detail::exact(*whole,*split,{},"Morph/Reverb held warm-up diverged");
    const std::vector<Point> points{{25,.875,37},{14,0,83},{15,100,173},
      {25,.125,256},{14,100,400},{15,0,400},{25,0,600}};
    partition(*whole,*split,points,{on(15,67,37),off(15,67,173),cc(64,127,0,400),off(0,60,600)},900);
    const auto state=saved(*whole);
    requireAutomation(state.morph.amount==0 && state.reverb.volumeLeft==1 && state.reverb.volumeRight==0,
      "Continuous Morph/Reverb authoring targets were not retained in host state");
    for(int n=0;n<8;++n)reverb_detail::exact(*whole,*split,{},"Continuous timeline diverged after carried automation");
  }
  for(int lifecycle=0;lifecycle<4;++lifecycle) {
    auto edited=make(),unity=make();gain_detail::warm(*edited,*unity);
    enqueue(*edited,{{0,0,600},{26,1.5,600},{0,25,1000},{26,3.8,1000}});
    reverb_detail::exact(*edited,*unity,{},"Future automation changed sound before lifecycle transition");
    if(lifecycle==0) { edited->resetForMidiTest(); unity->resetForMidiTest(); }
    else if(lifecycle==1) { edited->reactivateForMidiTest(); unity->reactivateForMidiTest(); }
    else if(lifecycle==2) {
      const auto wire=edited->stateBodyForTest();
      requireAutomation(edited->tryStateRestoreForTest(wire) &&
        unity->tryStateRestoreForTest(encodeState(builtin,100)),"Restore of latest future Gain targets failed");
    }
    else {
      edited->setBypassForGainTest(true);unity->setBypassForGainTest(true);
      requireAutomation(maximum(edited->renderMidi({}))==0 && maximum(unity->renderMidi({}))==0,
        "Automation lifecycle bypass did not mute voices");
      edited->setBypassForGainTest(false);unity->setBypassForGainTest(false);
    }
    for(int block=0;block<8;++block) {
      const auto events=block==0?std::vector<Midi>{on(0,60,37)}:std::vector<Midi>{};
      const auto a=edited->renderMidi(events),b=unity->renderMidi(events);
      for(std::size_t n=0;n<a.size();++n)
        requireAutomation(std::abs(double(a[n])-double(b[n])*.95)<3.e-8 &&
          std::abs(double(edited->right()[n])-double(unity->right()[n])*.95)<3.e-8,
          "Reset/reactivate/restore/bypass retained stale future automation or failed to snap latest Gain targets");
    }
  }
  std::cout<<"PASS "<<host<<" continuous automation sampleRate="<<rate
    <<" parameters5=true exactGainOffsets=true multiplePoints=true lastSameOffset=true futureCarry=true"
    <<" gain5msOracle=true gain380PCM=true boostStateRoundtrip=true malformedBoostAtomic=true schema10BoostReset=true gain0MappingStable=true"
    <<" morphReverbPartitionExact=true automationBeforeMIDI=true authoringTargetsSeparate=true lifecycleClearsFuture=true\n";
}

template<class Factory> void checkAuAutomationRamps(Factory make,double rate) {
  using namespace automation_detail;
  // A real ScheduleParameters batch uses immediate and ramp ingress, including
  // a ramp crossing host blocks and one already in progress at offset zero.
  for(int start:{37,-83}) {
    auto edited=make(),reference=make();gain_detail::warm(*edited,*reference);
    edited->scheduledRampForTest(0,100,25,start,400);
    edited->scheduledImmediateForTest(26,1.5,523);
    gain_detail::Line line(rate);
    int cursor=0,block=0;
    const int pattern[]{113,256,17,83,7,31,109};
    while(cursor<1000) {
      const int count=std::min(1000-cursor,pattern[block++%7]);
      const auto actual=edited->renderMidi({},count),baseline=reference->renderMidi({},count);
      for(int n=0;n<count;++n) {
        const int frame=cursor+n;
        double percent=100;
        if(frame>=start) percent=100.-75.*std::min(1.,double(frame-start)/400.);
        line.target(percent/100.*(frame>=523?1.5:1.));
        const auto coefficient=line.next();
        requireAutomation(std::abs(double(actual[n])-double(baseline[n])*coefficient)<3.e-8 &&
          std::abs(double(edited->right()[n])-double(reference->right()[n])*coefficient)<3.e-8,
          "AU ScheduleParameters ramp lost its start/end, negative elapsed time, or cross-block sample position");
      }
      cursor+=count;
    }
    requireAutomation(saved(*edited).gainPercent==25 && saved(*edited).gainBoost==1.5,
      "AU ramp saved a transient value instead of its authored endpoint");
  }
  std::cout<<"PASS AU ScheduleParameters sampleRate="<<rate
    <<" immediate=true gainRamp5msOracle=true positiveNegativeStart=true exactRampEndpoint=true crossBlock=true\n";
}
} // namespace tetorica::test
