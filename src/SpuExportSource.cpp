// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuExportSource.h"
#include "SpuBuiltin.h"
#include "InstrumentState.h"
#include <stdexcept>

namespace tetorica {
RawAdpcmSample extractAdpcmSource(const InstrumentDefinition& definition){
  using Kind=InstrumentDefinition::Kind;
  if(definition.kind()==Kind::AdpcmSample)return definition.rawAdpcmSample();
  RawAdpcmSample source;source.volumeLeft=source.volumeRight=1;
  if(definition.kind()==Kind::Builtin||definition.kind()==Kind::BuiltinSineWide){
    source.adpcm.assign(kBuiltinSineAdpcm.begin(),kBuiltinSineAdpcm.end());
    source.loopStartSamples=0;
  }else if(definition.kind()==Kind::PcmCycle){
    const auto encoded=encodePcmCycle(definition.pcmCycle());
    source.adpcm.assign(encoded.begin(),encoded.end());source.loopStartSamples=0;
    source.waveform=definition.pcmCycle();
  }else if(definition.kind()==Kind::Vab){
    const auto& bytes=definition.bankBytes();const auto bank=vab::parse({bytes.data(),bytes.size()});
    for(std::size_t i=0;i<bank.tones.size();++i)
      if(bank.tones[i].program==definition.program()&&bank.tones[i].index==definition.tone()){
        const auto tone=vab::prepareTone(bank,i);source.adpcm=tone.adpcm;
        source.baseFrequencyHz=tone.rootFrequencyHz;source.loopStartSamples=tone.loopStartSamples;
        source.envelope={tone.adsr1,tone.adsr2};source.reverb=tone.reverb;
        source.volumeLeft=tone.volumeLeft;source.volumeRight=tone.volumeRight;
        return source;
      }
    throw std::invalid_argument("SPU export: selected Program/Tone is absent");
  }else throw std::invalid_argument("SPU export: unsupported source kind");
  return source;
}
} // namespace tetorica
