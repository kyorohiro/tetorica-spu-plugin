// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuAdpcm.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace tetorica {
namespace {
[[noreturn]] void invalid(const char* message){throw std::invalid_argument(message);}
bool validName(const std::string& text){
  for(std::size_t at=0;at<text.size();){
    const auto first=static_cast<unsigned char>(text[at++]);
    if(first==0)return false;
    if(first<0x80)continue;
    unsigned length=0,code=0,minimum=0;
    if(first>=0xc2&&first<=0xdf){length=1;code=first&31;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){length=2;code=first&15;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){length=3;code=first&7;minimum=0x10000;}
    else return false;
    if(length>text.size()-at)return false;
    while(length--){const auto c=static_cast<unsigned char>(text[at++]);if((c&0xc0)!=0x80)return false;code=(code<<6)|(c&63);}
    if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
  }
  return true;
}
}
void validateRawAdpcmSample(const RawAdpcmSample& sample){
  const auto& bytes=sample.adpcm;
  if(bytes.empty()||bytes.size()%16||bytes.size()>vab::kMaxSampleBytes)invalid("Raw SPU ADPCM requires complete packets within520192 bytes");
  if(!std::isfinite(sample.baseFrequencyHz)||sample.baseFrequencyHz<=0)invalid("Raw SPU tuning must be positive finite Hz");
  for(double volume:{sample.volumeLeft,sample.volumeRight})
    if(!std::isfinite(volume)||volume< -1||volume>1)invalid("Raw SPU signed volume must be finite in [-1,1]");
  if(sample.inspectorPitch>0x3fff)invalid("Raw SPU inspector pitch must be0..16383");
  if(sample.name.size()>kMaxRawAdpcmNameBytes||!validName(sample.name))invalid("Raw SPU name must be bounded valid UTF8 without NUL");
  const auto frames=static_cast<std::uint32_t>(bytes.size()/16*28);
  std::optional<std::uint32_t> marker;
  for(std::size_t at=0;at<bytes.size();at+=16){
    const auto flags=bytes[at+1];
    if((bytes[at]>>4)>4||(bytes[at]&15)>12||(flags&~7)||bool(flags&1)!=(at+16==bytes.size()))invalid("Raw SPU ADPCM packet/header/END is invalid");
    if(flags&4){if(marker)invalid("Raw SPU ADPCM has multiple loop markers");marker=static_cast<std::uint32_t>(at/16*28);}
  }
  const bool loop=(bytes[bytes.size()-15]&2)!=0;
  if(loop!=sample.loopStartSamples.has_value()||(!loop&&marker))invalid("Raw SPU ADPCM loop metadata disagrees with END");
  if(sample.loopStartSamples&&(*sample.loopStartSamples>=frames||*sample.loopStartSamples%28))invalid("Raw SPU ADPCM repeat position must be an in-bounds packet boundary");
  if(marker&&marker!=sample.loopStartSamples)invalid("Raw SPU ADPCM loop marker disagrees with repeat position");
  if(sample.waveform){
    validatePcmCycle(*sample.waveform);
    if(sample.baseFrequencyHz!=44100./112||sample.loopStartSamples!=std::optional<std::uint32_t>(0)||bytes.size()!=64)
      invalid("Raw SPU authored PCM requires a112-frame loop with matching tuning");
    std::array<std::uint8_t,64> encoded{};std::copy(bytes.begin(),bytes.end(),encoded.begin());
    if(!matchesPcmCycleAdpcm(*sample.waveform,encoded))invalid("Raw SPU authored PCM and ADPCM do not match");
  }
}
bool sameRawAdpcmSample(const RawAdpcmSample& a,const RawAdpcmSample& b) noexcept {
  return a.adpcm==b.adpcm&&a.loopStartSamples==b.loopStartSamples&&
    !std::memcmp(&a.baseFrequencyHz,&b.baseFrequencyHz,sizeof(double))&&
    !std::memcmp(&a.volumeLeft,&b.volumeLeft,sizeof(double))&&
    !std::memcmp(&a.volumeRight,&b.volumeRight,sizeof(double))&&
    a.envelope.adsr1==b.envelope.adsr1&&a.envelope.adsr2==b.envelope.adsr2&&
    a.reverb==b.reverb&&a.inspectorPitch==b.inspectorPitch&&a.name==b.name&&
    a.waveform.has_value()==b.waveform.has_value()&&(!a.waveform||
      !std::memcmp(a.waveform->data(),b.waveform->data(),sizeof(float)*a.waveform->size()));
}
vab::PreparedTone prepareRawAdpcmSample(const RawAdpcmSample& sample){
  validateRawAdpcmSample(sample);vab::PreparedTone tone;
  tone.adpcm=sample.adpcm;tone.rootFrequencyHz=sample.baseFrequencyHz;
  tone.decodedFrames=static_cast<std::uint32_t>(sample.adpcm.size()/16*28);tone.loopStartSamples=sample.loopStartSamples;
  tone.adsr1=sample.envelope.adsr1;tone.adsr2=sample.envelope.adsr2;
  tone.volumeLeft=std::abs(sample.volumeLeft);tone.volumeRight=std::abs(sample.volumeRight);tone.reverb=sample.reverb;
  return tone;
}
} // namespace tetorica
