// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Real host parameter/MIDI ingress and offline PCM assertions for SPU Noise.
#pragma once
#include "pcm-cycle-checks.h"

namespace tetorica::test {
template<class Factory> void checkNoise(Factory make,double rate,const char* host,
                                      const std::vector<std::uint8_t>& bank) {
  using Bytes=std::vector<std::uint8_t>;
  const auto requireNoise=[](bool value,const char* message) {if(!value)throw std::runtime_error(message);};
  const auto builtin=InstrumentDefinition::builtin();
  const auto pcm=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  const auto vab=InstrumentDefinition::fromVab(bank,3,1);
  const EnvelopeSettings envelope{{0x000f,0x1fc0},true,false};
  const ReverbSettings effects{ReverbPreset::Room,false,false,.375,.625};
  const NoiseSettings generator{true,48};
  const auto events=std::vector<Midi>{tetorica::test::on(0,60,37)};
  const auto wire=encodeState(pcm,37,envelope,effects,generator);
  auto state=make(),reference=make();
  requireNoise(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"Noise state restore failed");
  requireNoise(state->stateBodyForTest()==wire,"Noise restore changed the nineteen-parameter tuple or retained PCM");
  reverb_detail::exact(*state,*reference,events,"First post-restore Noise PCM differs");
  requireNoise(maximum(state->right())>1e-4,"Noise first NoteOn produced no useful PCM");
  for(int i=0;i<16;++i)reverb_detail::exact(*state,*reference,{},"Restored Noise PCM diverged");
  auto decoded=envelope_detail::saved(*state);
  requireNoise(decoded.definition->kind()==InstrumentDefinition::Kind::PcmCycle&&decoded.definition->pcmCycle()==pcm->pcmCycle()&&
    decoded.gainPercent==37&&envelope_detail::equal(decoded.envelope,envelope)&&reverb_detail::same(decoded.reverb,effects)&&
    decoded.noise.enabled&&decoded.noise.clock==48,"Noise authoring tuple or source PCM did not round-trip");

  // Clock is independent from musical pitch. Identical per-instance noise
  // histories must be bit-identical under different keys/channels and wheels.
  for(int bendValue:{0,8192,16383}) {
    auto low=make(),high=make();
    requireNoise(low->tryStateRestoreForTest(wire)&&high->tryStateRestoreForTest(wire),"Noise pitch fixtures rejected");
    const auto a=low->renderMidi({bend(0,0),on(0,60)});
    const auto b=high->renderMidi({bend(bendValue,15),on(15,72)});
    requireNoise(a==b&&low->right()==high->right(),"Noise incorrectly followed Note/Bend pitch");
    for(int i=0;i<32;++i)reverb_detail::exact(*low,*high,{},"Noise key/channel changed shared clock output");
  }
  // A wheel update while held must not retrigger or perturb a Noise voice.
  const auto a=state->renderMidi({bend(16383,0)}),b=reference->renderMidi({});
  requireNoise(a==b&&state->right()==reference->right(),"Held Noise pitch bend changed its samples or retriggered");
  state->plainParameterForTest(18,48); // Repeated same clock is an exact no-op.
  reverb_detail::exact(*state,*reference,{},"Repeated Noise clock value reset the generator");
  state->plainParameterForTest(18,8); // No new NoteOn: this must affect the held voice.
  std::vector<float> changed,unchanged;
  for(int i=0;i<16;++i) {
    const auto x=state->renderMidi({}),y=reference->renderMidi({});
    changed.insert(changed.end(),x.begin(),x.end());unchanged.insert(unchanged.end(),y.begin(),y.end());
  }
  requireNoise(maximum(changed)>1e-4&&delta(changed,unchanged)>1e-3,"Noise clock automation did not update a held note live");
  decoded=envelope_detail::saved(*state);
  requireNoise(decoded.noise.enabled&&decoded.noise.clock==8&&decoded.definition->pcmCycle()==pcm->pcmCycle(),
    "Noise clock automation changed the retained source or was not saved");

  // Toggle topology between render calls: previous held keys and future old
  // notes are discarded. A fresh NoteOn in the toggle block is retained.
  auto topology=make(),dry=make();
  requireNoise(topology->tryStateRestoreForTest(wire),"Noise topology fixture rejected");
  topology->renderMidi({on(),on(0,67,600)});
  topology->plainParameterForTest(17,0);
  requireNoise(maximum(topology->renderMidi({}))==0,"Noise-off retained a held voice");
  requireNoise(maximum(topology->renderMidi({}))==0&&maximum(topology->renderMidi({}))==0,
    "Noise mode change replayed a carried old note");
  requireNoise(dry->tryStateRestoreForTest(encodeState(pcm,37,envelope,effects)),"Retained PCM reference rejected");
  // Mode panic resets the causal resampler, then the topology instance has
  // already advanced 768 silent host frames. Advance the dry reference by the
  // same three blocks before comparing: at 48/96k a fresh adapter has a
  // different fractional native-sample phase even while its input is silent.
  for(int block=0;block<3;++block)
    requireNoise(maximum(dry->renderMidi({}))==0,"Retained PCM reference was not initially silent");
  reverb_detail::exact(*topology,*dry,events,"Noise-off did not resume the retained PCM source");
  topology->plainParameterForTest(17,1);
  requireNoise(maximum(topology->renderMidi({}))==0,"Noise-on retained the preceding pitched voice");
  topology->plainParameterForTest(17,0);
  auto fresh=make();requireNoise(fresh->tryStateRestoreForTest(encodeState(pcm,37,envelope,effects)),"Fresh mode-change reference rejected");
  reverb_detail::exact(*topology,*fresh,events,"Topology panic discarded a fresh current-block NoteOn");

  // Validate before adopting as well as during held Noise. Invalid extension
  // data must preserve every old parameter and the immutable pending source.
  requireNoise(state->tryStateRestoreForTest(wire)&&reference->tryStateRestoreForTest(wire),"Noise atomic fixtures rejected");
  std::vector<Bytes> invalid;
  for(auto flags:{2u,4u,0xffffffffu}) {auto bad=wire;envelope_detail::word(bad,72+448,flags);invalid.push_back(bad);}
  for(auto clock:{64u,0xffffffffu}) {auto bad=wire;envelope_detail::word(bad,76+448,clock);invalid.push_back(bad);}
  auto bad=wire;bad.pop_back();invalid.push_back(bad);
  bad=wire;envelope_detail::word(bad,8,8);invalid.push_back(bad);
  for(const auto& bytes:invalid)
    requireNoise(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,"Malformed Noise state cancelled a pending restore");
  reverb_detail::exact(*state,*reference,events,"Rejected Noise state altered the first restored NoteOn");
  for(const auto& bytes:invalid) {
    requireNoise(!state->tryStateRestoreForTest(bytes)&&state->stateBodyForTest()==wire,"Malformed Noise state changed active parameters/source");
    reverb_detail::exact(*state,*reference,{},"Malformed Noise state interrupted held PCM");
  }

  // Old schemas must actively reset Noise, even when replacing a held Noise
  // instrument. Schema4 also retains the authored PCM and complete FX tuple.
  for(unsigned schema:{1u,2u,3u,4u}) {
    const auto source=schema==4?pcm:vab;
    const auto size=schema==4?448:bank.size();
    auto old=encodeState(source,37,envelope,effects,generator);
    old.resize(size+(schema==1?40:schema==2?48:72));
    envelope_detail::word(old,8,schema);envelope_detail::word(old,12,static_cast<std::uint32_t>(old.size()));
    const auto oldEnvelope=schema>=2?envelope:EnvelopeSettings{};
    const auto oldEffects=schema>=3?effects:ReverbSettings{};
    requireNoise(state->tryStateRestoreForTest(wire),"Noise migration fixture rejected");
    requireNoise(maximum(state->renderMidi({on()}))>1e-4,"Noise migration fixture did not sound");
    requireNoise(state->tryStateRestoreForTest(old)&&
      reference->tryStateRestoreForTest(encodeState(source,37,oldEnvelope,oldEffects)),"Old schema migration from Noise failed");
    decoded=envelope_detail::saved(*state);
    requireNoise(!decoded.noise.enabled&&decoded.noise.clock==48&&
      decoded.definition->kind()==source->kind()&&decoded.definition->pcmCycle()==source->pcmCycle()&&
      decoded.definition->bankBytes()==source->bankBytes()&&envelope_detail::equal(decoded.envelope,oldEnvelope)&&
      reverb_detail::same(decoded.reverb,oldEffects),"Old schema migration lost source/FX or retained Noise");
    reverb_detail::exact(*state,*reference,events,"Old schema first-block audio changed after Noise migration");
  }
  Bytes legacy(8);reverb_detail::putDouble(legacy,0,17);
  requireNoise(state->tryStateRestoreForTest(wire),"Legacy Noise migration fixture rejected");
  state->renderMidi({on()});
  requireNoise(state->tryStateRestoreForTest(legacy)&&
    reference->tryStateRestoreForTest(encodeState(builtin,17)),"Legacy migration over Noise failed");
  decoded=envelope_detail::saved(*state);
  requireNoise(!decoded.noise.enabled&&decoded.noise.clock==48&&decoded.definition->kind()==InstrumentDefinition::Kind::Builtin,
    "Legacy migration retained Noise or an imported source");
  reverb_detail::exact(*state,*reference,events,"Legacy Gain-only PCM changed after Noise migration");
  // Builtin and VAB remain independently saveable sources while Noise is on.
  for(const auto& source:{builtin,vab}) {
    const auto sourceWire=encodeState(source,37,envelope,effects,{true,63});
    requireNoise(state->tryStateRestoreForTest(sourceWire)&&state->stateBodyForTest()==sourceWire,"Noise discarded a Builtin/VAB source");
    requireNoise(maximum(state->renderMidi({on()}))>1e-4,"Retained Builtin/VAB Noise source did not sound");
    state->plainParameterForTest(17,0);
    requireNoise(reference->tryStateRestoreForTest(encodeState(source,37,envelope,effects)),"Noise source-off reference failed");
    reverb_detail::exact(*state,*reference,events,"Noise-off failed to use its retained Builtin/VAB source");
  }
  std::cout<<"PASS "<<host<<" Noise sampleRate="<<rate
    <<" full19Params=true firstRestoreBlock=true noNoteBendTracking=true clockLive=true sameClockNoop=true"
    <<" topologyPanic=true oldCarriedNotesCleared=true freshNotePreserved=true sourceRetained=true malformedAtomic=true schema1To4Legacy=true schema4PCMFX=true\n";
}
} // namespace tetorica::test
