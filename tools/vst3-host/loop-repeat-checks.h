// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Actual bundles must preserve original flags and loop without a repeat bit
// on the target packet. Compare to identical decoded data with flags6/2.
#pragma once
#include "web-instrument-checks.h"
#include "WebInstrumentExport.h"
#include "SpuExportSource.h"

namespace tetorica::test {
template<class Factory>void checkLoopRepeats(Factory make,double rate,const char* host,
                                            const std::vector<std::uint8_t>& bank) {
  const auto requireLoop=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
  using Def=InstrumentDefinition;
  struct Case{std::shared_ptr<const Def> original,reference;};std::vector<Case> cases;
  constexpr auto body=3616+16;
  for(bool marker:{false,true}){
    auto source=bank;source[body+1]=0;source[body+17]=marker?4:0;
    auto canonical=source;canonical[body+(marker?17:1)]|=2;
    cases.push_back({Def::fromVab(source,3,0),Def::fromVab(canonical,3,0)});
    auto raw=web_detail::fixture("v1-waveform.spu.json").state.definition->rawAdpcmSample();
    raw.waveform.reset();raw.adpcm[1]=0;raw.adpcm[17]=marker?4:0;
    raw.loopStartSamples=28;raw.baseFrequencyHz=123.5;raw.volumeLeft=-.5;raw.volumeRight=.25;
    auto rawReference=raw;rawReference.adpcm[17]|=2;
    cases.push_back({Def::fromAdpcmSample(raw),Def::fromAdpcmSample(rawReference)});
  }
  for(const auto& c:cases){
    SavedInstrumentState state;state.definition=c.original;state.gainPercent=37;
    state.envelope={{0xa937,0xd7bb},false,false};state.reverb={ReverbPreset::Room,false,true,.375,.625};
    state.noise={false,37};state.pmon={false,WaveformShape::Saw,2.5,.375,1250};state.morph={false,.375};
    auto referenceState=state;referenceState.definition=c.reference;
    const auto original=web_detail::wire(state);auto a=make(),reference=make();
    requireLoop(a->tryStateRestoreForTest(original)&&reference->tryStateRestoreForTest(web_detail::wire(referenceState)),
      "Actual bundle rejected flag4-only or markerless flag0 repeat target");
    requireLoop(a->stateBodyForTest()==original,"Loop restore/save rewrote original ADPCM flags or full26 settings");
    reverb_detail::exact(*a,*reference,{on(0,60,37)},"Original repeat flags changed first post-restore PCM");
    requireLoop(std::all_of(a->right().begin(),a->right().begin()+37,[](float x){return x==0;}),
      "Repeat fixture sounded before NoteOn offset");
    for(int n=0;n<32;++n)reverb_detail::exact(*a,*reference,{},"Original repeat flags stopped/changed loop PCM after END");
    requireLoop(maximum(a->right())>1e-5,"Supported repeat target became silent after END");
    // A real host save embeds the whole original bank or raw bytes. Reopening
    // requires no external path and must also begin with identical fresh PCM.
    auto reopened=make();requireLoop(reopened->tryStateRestoreForTest(a->stateBodyForTest()),"Saved original repeat state did not reopen");
    requireLoop(a->tryStateRestoreForTest(original),"Original repeat re-restore failed");
    reverb_detail::exact(*a,*reopened,{on(0,60,37)},"Original repeat embedded restore changed initial PCM");
    const auto exported=writeWebInstrument(envelope_detail::saved(*a),"Original repeat flags");
    const auto fromWeb=readWebInstrument(exported.json);
    const auto source=extractAdpcmSource(*c.original);
    requireLoop(fromWeb.state.definition->rawAdpcmSample().adpcm==source.adpcm&&
      fromWeb.state.definition->rawAdpcmSample().loopStartSamples==source.loopStartSamples,
      "Web export/import rewrote original repeat flags or target");
    // Both host PCM banks may temporarily hide the physical ADPCM source.
    // Returning from Noise/PMON/Morph must preserve the prepared repeat target.
    for(int mode=0;mode<3;++mode){
      a->plainParameterForTest(17,mode==0?1:0);a->plainParameterForTest(19,mode==1?1:0);
      a->plainParameterForTest(24,mode==2?1:0);a->renderMidi({on()});
      a->plainParameterForTest(17,0);a->plainParameterForTest(19,0);a->plainParameterForTest(24,0);
      a->renderMidi({on()});
      requireLoop(maximum(blocks(*a,16))>1e-5,"Host-bank mode return lost original RAM repeat target");
    }
  }
  std::cout<<"PASS "<<host<<" LoopRepeat sampleRate="<<rate
    <<" VABRaw4Cases=true marker4=true markerless0=true nonzeroRepeat=true originalBytes=true"
    <<" full26Params=true firstRestoreBlock=true PCMExactToFlag6Or2=true continuesAfterEND=true"
    <<" embeddedReopen=true WebOriginalFlags=true hostBankModeReturn=true\n";
}
} // namespace tetorica::test
