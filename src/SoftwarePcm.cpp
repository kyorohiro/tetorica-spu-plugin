// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SoftwarePcm.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace tetorica {
namespace {
[[noreturn]] void invalid(const char* reason){throw std::invalid_argument(reason);}
void frames(std::size_t count){if(!count||count>kMaxSoftwarePcmFrames)invalid("Software PCM requires 1 frame to 32 MiB of mono PCM16");}
std::int16_t signedWord(unsigned word){return static_cast<std::int16_t>(word<32768?static_cast<int>(word):static_cast<int>(word)-65536);}
bool validName(const std::string& text){
  for(std::size_t at=0;at<text.size();){
    const auto first=static_cast<unsigned char>(text[at++]);
    if(!first)return false;if(first<0x80)continue;
    unsigned count=0,code=0,minimum=0;
    if(first>=0xc2&&first<=0xdf){count=1;code=first&31;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){count=2;code=first&15;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){count=3;code=first&7;minimum=0x10000;}
    else return false;
    if(count>text.size()-at)return false;
    while(count--){const auto next=static_cast<unsigned char>(text[at++]);if((next&0xc0)!=0x80)return false;code=(code<<6)|(next&63);}
    if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))return false;
  }
  return true;
}
int digit(unsigned char value){
  if(value>='A'&&value<='Z')return value-'A';
  if(value>='a'&&value<='z')return value-'a'+26;
  if(value>='0'&&value<='9')return value-'0'+52;
  if(value=='+')return 62;if(value=='/')return 63;return -1;
}
}
SoftwarePcmBuffer::SoftwarePcmBuffer(std::vector<std::int16_t> samples):mSamples(std::move(samples)){}
std::shared_ptr<const SoftwarePcmBuffer> SoftwarePcmBuffer::copyOf(const std::vector<std::int16_t>& samples){
  frames(samples.size());
  return std::shared_ptr<const SoftwarePcmBuffer>(new SoftwarePcmBuffer(samples));
}
std::shared_ptr<const SoftwarePcmBuffer> SoftwarePcmBuffer::fromLittleEndian(vab::BytesView bytes){
  if(!bytes.data||!bytes.size||bytes.size%2||bytes.size>kMaxSoftwarePcmBytes)
    invalid("Software PCM requires complete nonempty PCM16LE within 32 MiB");
  std::vector<std::int16_t> samples(bytes.size/2);
  for(std::size_t i=0;i<samples.size();++i)samples[i]=signedWord(unsigned(bytes.data[i*2])|(unsigned(bytes.data[i*2+1])<<8));
  return std::shared_ptr<const SoftwarePcmBuffer>(new SoftwarePcmBuffer(std::move(samples)));
}
void validateSoftwarePcmSample(const SoftwarePcmSample& sample){
  if(!sample.pcm)invalid("Software PCM source is missing its immutable buffer");
  frames(sample.pcm->samples().size());
  if(sample.loop&&sample.pcm->samples().size()<4)invalid("Software PCM loops require at least 4 frames");
  if(!std::isfinite(sample.baseFrequencyHz)||sample.baseFrequencyHz<.000001||sample.baseFrequencyHz>1000000)
    invalid("Software PCM tuning must be finite 0.000001 to 1000000 Hz");
  for(double volume:{sample.volumeLeft,sample.volumeRight})
    if(!std::isfinite(volume)||volume< -1||volume>1)invalid("Software PCM signed volume must be finite in [-1,1]");
  if(sample.inspectorPitch>0x3fff)invalid("Software PCM inspector pitch must be 0..16383");
  if(sample.name.size()>kMaxSoftwarePcmNameBytes||!validName(sample.name))invalid("Software PCM name must be bounded valid UTF8 without NUL");
}
bool sameSoftwarePcmSample(const SoftwarePcmSample& a,const SoftwarePcmSample& b) noexcept {
  return a.loop==b.loop&&!std::memcmp(&a.baseFrequencyHz,&b.baseFrequencyHz,sizeof(double))&&
    !std::memcmp(&a.volumeLeft,&b.volumeLeft,sizeof(double))&&!std::memcmp(&a.volumeRight,&b.volumeRight,sizeof(double))&&
    a.envelope.adsr1==b.envelope.adsr1&&a.envelope.adsr2==b.envelope.adsr2&&a.reverb==b.reverb&&
    a.inspectorPitch==b.inspectorPitch&&a.name==b.name&&
    (a.pcm==b.pcm||(a.pcm&&b.pcm&&a.pcm->samples()==b.pcm->samples()));
}
std::shared_ptr<const SoftwarePcmBuffer> decodeSoftwarePcmBase64(std::string_view text){
  constexpr std::size_t maximum=((kMaxSoftwarePcmBytes+2)/3)*4;
  if(text.empty()||text.size()%4||text.size()>maximum)invalid("Software PCM base64 exceeds its bound or has invalid length");
  const unsigned padding=(text.back()=='=')+(text[text.size()-2]=='=');
  const auto symbols=text.size()-padding;
  for(std::size_t i=0;i<symbols;++i)if(digit(static_cast<unsigned char>(text[i]))<0)invalid("Software PCM base64 has an invalid alphabet or padding");
  for(std::size_t i=symbols;i<text.size();++i)if(text[i]!='=')invalid("Software PCM base64 has invalid padding");
  if((padding==2&&(digit(static_cast<unsigned char>(text[symbols-1]))&15))||
     (padding==1&&(digit(static_cast<unsigned char>(text[symbols-1]))&3)))
    invalid("Software PCM base64 padding bits must be canonical");
  const auto byteCount=text.size()/4*3-padding;
  if(!byteCount||byteCount%2||byteCount>kMaxSoftwarePcmBytes)invalid("Software PCM base64 must contain complete PCM16LE within 32 MiB");
  std::vector<std::int16_t> samples(byteCount/2);std::size_t byteAt=0;unsigned low=0;
  const auto emit=[&](unsigned byte){if(byteAt%2) samples[byteAt/2]=signedWord(low|(byte<<8));else low=byte;++byteAt;};
  for(std::size_t at=0;at<text.size();at+=4){
    unsigned packed=0;
    for(unsigned i=0;i<4;++i)packed=(packed<<6)|(text[at+i]=='='?0u:static_cast<unsigned>(digit(static_cast<unsigned char>(text[at+i]))));
    for(int shift:{16,8,0})if(byteAt<byteCount)emit((packed>>shift)&255);
  }
  return std::shared_ptr<const SoftwarePcmBuffer>(new SoftwarePcmBuffer(std::move(samples)));
}
void appendSoftwarePcmBase64(const SoftwarePcmBuffer& pcm,std::string& output){
  const auto& samples=pcm.samples();frames(samples.size());
  const auto byteCount=samples.size()*2,encoded=(byteCount+2)/3*4;
  if(encoded>output.max_size()-output.size())throw std::length_error("Software PCM base64 output exceeds string capacity");
  output.reserve(output.size()+encoded);
  const auto byte=[&](std::size_t at){return (static_cast<unsigned>(static_cast<std::uint16_t>(samples[at/2]))>>((at%2)*8))&255;};
  constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for(std::size_t at=0;at<byteCount;at+=3){
    const auto remaining=byteCount-at;
    const unsigned packed=(byte(at)<<16)|(remaining>1?byte(at+1)<<8:0)|(remaining>2?byte(at+2):0);
    output.push_back(alphabet[(packed>>18)&63]);output.push_back(alphabet[(packed>>12)&63]);
    output.push_back(remaining>1?alphabet[(packed>>6)&63]:'=');output.push_back(remaining>2?alphabet[packed&63]:'=');
  }
}
} // namespace tetorica
