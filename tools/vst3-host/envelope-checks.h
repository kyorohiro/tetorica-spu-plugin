// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual AU/VST3 host checks: no production test hooks or audio device.
#pragma once
#include "InstrumentState.h"
#include "midi-controller-checks.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace tetorica::test {
namespace envelope_detail {
using Bytes=std::vector<std::uint8_t>;
inline void requireEnvelope(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
inline bool equal(EnvelopeSettings a,EnvelopeSettings b) {
  return a.custom.adsr1==b.custom.adsr1&&a.custom.adsr2==b.custom.adsr2&&a.enabled==b.enabled&&a.live==b.live;
}
inline void word(Bytes& bytes,std::size_t at,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));
}
inline double envelopeEnergy(const std::vector<float>& pcm) {
  double total=0;for(float sample:pcm)total+=sample*sample;return total;
}
inline double envelopePeak(const std::vector<float>& pcm,std::size_t first=0) {
  double result=0;for(std::size_t i=first;i<pcm.size();++i)result=std::max(result,std::abs(double(pcm[i])));return result;
}
// Public host IDs are kept stable: Gain=0, nine raw fields=2..10,
// Custom ADSR=1, Apply while playing=11. Values here are plain AU units;
// the VST3 host adapter converts through IEditController's official mapping.
template<class Instance> void author(Instance& instance,EnvelopeSettings s) {
  const auto a=s.custom.adsr1,b=s.custom.adsr2;
  instance.plainParameterForTest(1,s.enabled?1:0);
  instance.plainParameterForTest(2,(a>>8)&127);
  instance.plainParameterForTest(3,(a>>15)&1);
  instance.plainParameterForTest(4,(a>>4)&15);
  instance.plainParameterForTest(5,a&15);
  instance.plainParameterForTest(6,(b>>6)&127);
  instance.plainParameterForTest(7,(b>>14)&1);
  instance.plainParameterForTest(8,(b>>15)&1);
  instance.plainParameterForTest(9,b&31);
  instance.plainParameterForTest(10,(b>>5)&1);
  instance.plainParameterForTest(11,s.live?1:0);
}
template<class Instance> SavedInstrumentState saved(Instance& instance) {
  const auto bytes=instance.stateBodyForTest();return decodeState({bytes.data(),bytes.size()});
}
}

template<class Factory> void checkEnvelopes(Factory make,double rate,const char* host,
                                          const std::vector<std::uint8_t>& bank) {
  using namespace envelope_detail;
  const std::vector<Midi> on{{0x90,60,127,37}};
  const auto selected=InstrumentDefinition::fromVab(bank,3,1);
  const EnvelopeSettings authored{{0xa937,0xd7bb},false,false}; //Every one of the nine fields is non-default.
  auto edited=make(),reference=make();
  requireEnvelope(edited->tryStateRestoreForTest(encodeState(selected,37)),"ADSR fixture restore failed");
  author(*edited,authored);edited->renderMidi({});
  const auto serialized=edited->stateBodyForTest();
  auto decoded=decodeState({serialized.data(),serialized.size()});
  requireEnvelope(equal(decoded.envelope,authored)&&decoded.gainPercent==37&&decoded.definition->bankBytes()==bank&&
    decoded.definition->program()==3&&decoded.definition->tone()==1,"Nine ADSR parameter fields/flags or source Tone failed state serialization");
  requireEnvelope(reference->tryStateRestoreForTest(serialized),"Authored ADSR state restore failed");
  // Reset the edited engine too: compare the exact immediate first block,
  // including current NoteOn offset, without a preceding warm-up process call.
  requireEnvelope(edited->tryStateRestoreForTest(serialized),"ADSR immediate re-restore failed");
  requireEnvelope(edited->renderMidi(on)==reference->renderMidi(on),"First post-restore ADSR PCM differs");
  for(int i=0;i<8;++i)requireEnvelope(edited->renderMidi({})==reference->renderMidi({}),"Restored ADSR PCM diverged");

  std::vector<Bytes> invalid;
  auto bad=serialized;word(bad,44+bank.size(),4);invalid.push_back(bad);
  bad=serialized;bad[43+bank.size()]|=0x20;invalid.push_back(bad);
  bad=serialized;word(bad,8,7);invalid.push_back(bad);
  bad=serialized;bad.pop_back();invalid.push_back(bad);
  for(const auto& bytes:invalid) {
    requireEnvelope(!edited->tryStateRestoreForTest(bytes),"Malformed ADSR state accepted");
    requireEnvelope(edited->stateBodyForTest()==serialized,"Rejected ADSR state changed a parameter/source snapshot");
    requireEnvelope(edited->renderMidi({})==reference->renderMidi({}),"Rejected ADSR state interrupted held PCM");
  }

  // Schema1 has no custom extension and must migrate to original source ADSR,
  // even when replacing a live schema2 override with all fields authored.
  auto old=serialized;old.resize(40+bank.size());word(old,8,1);word(old,12,static_cast<std::uint32_t>(old.size()));
  requireEnvelope(edited->tryStateRestoreForTest(old),"Schema1 ADSR migration rejected");
  requireEnvelope(equal(saved(*edited).envelope,{})&&saved(*edited).definition->bankBytes()==bank,"Schema1 failed to restore default override/original VAB");
  requireEnvelope(reference->tryStateRestoreForTest(encodeState(selected,37)),"Original ADSR reference restore failed");
  requireEnvelope(edited->renderMidi(on)==reference->renderMidi(on),"Schema1 original ADSR first-block PCM changed");
  Bytes legacy(8);constexpr std::uint64_t legacyGain=0x4031000000000000ull; //17.0, binary64 LE.
  for(unsigned i=0;i<8;++i)legacy[i]=static_cast<std::uint8_t>(legacyGain>>(i*8));
  requireEnvelope(edited->tryStateRestoreForTest(serialized)&&edited->tryStateRestoreForTest(legacy),
    "Legacy Gain migration over authored ADSR failed");
  const auto migratedLegacy=saved(*edited);
  requireEnvelope(equal(migratedLegacy.envelope,{})&&migratedLegacy.gainPercent==17&&
    migratedLegacy.definition->kind()==InstrumentDefinition::Kind::Builtin,"Legacy Gain retained authored ADSR/source");

  // The same source with Custom ADSR disabled must remain byte-identical to
  // the unmodified instrument. Enabling a very slow attack must change PCM.
  const auto builtin=InstrumentDefinition::builtin();
  const EnvelopeSettings slow{{0x640f,0x1fc0},true,true};
  auto fast=make(),slowRestored=make(),slowAutomated=make(),off=make();
  auto disabled=slow;disabled.enabled=false;
  requireEnvelope(off->tryStateRestoreForTest(encodeState(builtin,100,disabled)),"Disabled custom ADSR restore failed");
  requireEnvelope(slowRestored->tryStateRestoreForTest(encodeState(builtin,100,slow)),"Slow ADSR restore failed");
  author(*slowAutomated,slow); // Host automation arrives before this process boundary.
  const auto original=fast->renderMidi(on),disabledPCM=off->renderMidi(on);
  const auto restoredPCM=slowRestored->renderMidi(on),automatedPCM=slowAutomated->renderMidi(on);
  requireEnvelope(original==disabledPCM,"Disabled custom ADSR changed original source PCM");
  requireEnvelope(restoredPCM==automatedPCM,"Block-boundary ADSR automation differs from restored settings");
  requireEnvelope(envelopePeak(restoredPCM)<envelopePeak(original)*.01,"Custom ADSR enable/attack-rate automation had no audible effect");
  requireEnvelope(equal(saved(*slowAutomated).envelope,slow),"Automated ADSR settings did not serialize");

  // Apply-off changes the next key's envelope while preserving the old held
  // envelope. Compare against a pristine engine with the same deferred tuple.
  auto next=make(),nextReference=make();auto deferredSlow=slow;deferredSlow.live=false;
  author(*next,deferredSlow);
  requireEnvelope(nextReference->tryStateRestoreForTest(encodeState(builtin,100,deferredSlow)),"Deferred next-note restore failed");
  requireEnvelope(next->renderMidi(on)==nextReference->renderMidi(on),"Apply-off ignored ADSR on the next NoteOn");

  // Editing only Attack and SL while already in Sustain must not retrigger or
  // reset oscillator phase/level. Changing Sustain while Apply is off remains
  // deferred; turning Apply on with identical raw words must flush it now.
  auto live=make(),held=make();
  requireEnvelope(live->renderMidi(on)==held->renderMidi(on),"Held ADSR fixtures differ");
  for(int i=0;i<8;++i)requireEnvelope(live->renderMidi({})==held->renderMidi({}),"Held source PCM differs");
  author(*live,{{0x7f03,0x1fc0},true,true});
  requireEnvelope(live->renderMidi({})==held->renderMidi({}),"Live Attack/SL edit retriggered a sustained voice");
  author(*live,{{0x7f03,0x4a00},true,false});
  for(int i=0;i<4;++i)requireEnvelope(live->renderMidi({})==held->renderMidi({}),"Apply-off changed the held voice");
  live->plainParameterForTest(11,1); // No rate/word change: Apply edge alone must refresh.
  std::vector<float> changed,unchanged;
  for(int i=0;i<static_cast<int>(std::ceil(rate/44100.*16));++i) {
    const auto a=live->renderMidi({}),b=held->renderMidi({});
    changed.insert(changed.end(),a.begin(),a.end());unchanged.insert(unchanged.end(),b.begin(),b.end());
  }
  requireEnvelope(changed.front()==unchanged.front(),"Live refresh restarted the oscillator/sample cursor");
  requireEnvelope(envelopeEnergy(changed)<envelopeEnergy(unchanged)*.5&&envelopePeak(changed,changed.size()-256)==0&&envelopePeak(unchanged)>.01,
    "Apply-on did not refresh the deferred sustain rate on an already sounding voice");
  std::cout<<"PASS "<<host<<" ADSR sampleRate="<<rate
    <<" nineFieldsState=true schema1Migration=true legacyDefaults=true malformedAtomic=true firstRestoreBlock=true"
    <<" sourcePreserved=true disabledOriginalPCM=true blockAutomation=true nextNoteDeferred=true heldNoRetrigger=true applyOffOnRefresh=true\n";
}
} // namespace tetorica::test
