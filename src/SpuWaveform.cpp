// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Port of the original Web encodePs1AdpcmCycle in docs/js/ps1_adpcm_encoder.js
// of hello_ymfm. Its prediction arithmetic follows the retained BSD-3-Clause
// pSXAuthor/R. Belmont SPU decoder; this file does not render or emulate it.
#include "SpuWaveform.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace tetorica {
namespace {
constexpr int filters[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
constexpr double pi=3.14159265358979323846264338327950288;
using AdpcmCycle=std::array<std::uint8_t,64>;

int prediction(int p1,int p2,int filter) {
  // Equivalent to JS's signed >>6, including negative values. Avoid depending
  // on implementation-defined right shift of a negative integer in C++17.
  const int sum=p1*filters[filter][0]+p2*filters[filter][1]+32;
  return sum>=0?sum/64:-((-sum+63)/64);
}
AdpcmCycle encode(const PcmCycle& pcm,std::optional<std::array<int,2>> history) {
  AdpcmCycle output{};
  std::array<double,28> targets{};
  std::array<int,28> nibbles{},samples{},bestNibbles{},bestSamples{};
  int history1=history?(*history)[0]:0,history2=history?(*history)[1]:0;
  for(unsigned block=0;block<4;++block){
    for(unsigned n=0;n<28;++n)targets[n]=double(pcm[block*28+n])*32768;
    const int lastFilter=history||block!=0?4:0;
    double bestError=std::numeric_limits<double>::infinity();
    int bestFilter=0,bestShift=0;
    for(int filter=0;filter<=lastFilter;++filter){
      for(int shift=0;shift<=12;++shift){
        const int step=4096>>shift;
        int p1=history1,p2=history2;double error=0;
        for(unsigned n=0;n<28;++n){
          const int predicted=prediction(p1,p2,filter);
          const int minNibble=std::max(-8,static_cast<int>(std::ceil(double(-32768-predicted)/step)));
          const int maxNibble=std::min(7,static_cast<int>(std::floor(double(32767-predicted)/step)));
          if(minNibble>maxNibble){error=std::numeric_limits<double>::infinity();break;}
          // Halfway ties choose the smaller signed nibble, as in the Web
          // encoder's exhaustive nearest search. Every unwrapped sum fits i16.
          const int nibble=std::clamp(static_cast<int>(std::ceil((targets[n]-predicted)/step-.5)),minNibble,maxNibble);
          const int value=predicted+nibble*step;
          const double difference=targets[n]-value;
          nibbles[n]=nibble;samples[n]=value;p2=p1;p1=value;
          error+=difference*difference;
          if(error>=bestError)break;
        }
        if(error<bestError){
          bestError=error;bestFilter=filter;bestShift=shift;
          bestNibbles=nibbles;bestSamples=samples;
        }
        if(bestError==0)break;
      }
      if(bestError==0)break;
    }
    const unsigned at=block*16;
    output[at]=static_cast<std::uint8_t>((bestFilter<<4)|bestShift);
    output[at+1]=static_cast<std::uint8_t>(block==0?6:block==3?3:0);
    for(unsigned n=0;n<28;n+=2)
      output[at+2+n/2]=static_cast<std::uint8_t>((bestNibbles[n]&15)|((bestNibbles[n+1]&15)<<4));
    history1=bestSamples[27];history2=bestSamples[26];
  }
  return output;
}
std::optional<double> scoreLoop(const AdpcmCycle& adpcm,const PcmCycle& pcm) {
  int p1=0,p2=0;double error=0;
  for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<112;++i){
    const unsigned at=i/28*16,header=adpcm[at],packed=adpcm[at+2+(i%28)/2];
    const int nibble=static_cast<int>((packed>>((i%2)*4))&15);
    const int value=(nibble<8?nibble:nibble-16)*(4096>>(header&15))+prediction(p1,p2,header>>4);
    if(value< -32768||value>32767)return std::nullopt;
    p2=p1;p1=value;
    if(pass){const double difference=double(pcm[i])*32768-value;error+=difference*difference;}
  }
  // Match the Web comparison after RMS rounding, not merely its mathematically
  // monotonic squared sum: two neighboring sums can round to the same root.
  return std::sqrt(error/112)/32768;
}
}

PcmCycle makePcmCycle(WaveformShape shape) {
  if(shape!=WaveformShape::Sine&&shape!=WaveformShape::Triangle&&
      shape!=WaveformShape::Saw&&shape!=WaveformShape::Square)
    throw std::invalid_argument("SPU waveform: unknown shape");
  PcmCycle pcm{};
  for(unsigned i=0;i<pcm.size();++i){
    const double phase=double(i)/pcm.size();double value=0;
    switch(shape){
      case WaveformShape::Sine:value=std::sin(2*pi*phase);break;
      case WaveformShape::Triangle:value=2/pi*std::asin(std::sin(2*pi*phase));break;
      case WaveformShape::Saw:value=2*phase-1;break;
      case WaveformShape::Square:value=phase<.5?1:-1;break;
    }
    pcm[i]=static_cast<float>(.8*value);
  }
  return pcm;
}
void validatePcmCycle(const PcmCycle& pcm) {
  for(float value:pcm)if(!std::isfinite(value)||value< -1||value>1)
    throw std::invalid_argument("SPU waveform: PCM must be finite in [-1,1]");
}
std::array<std::uint8_t,64> encodePcmCycle(const PcmCycle& pcm) {
  validatePcmCycle(pcm);
  const auto independent=encode(pcm,std::nullopt);
  const auto sample=[](float value){
    // JS Math.round breaks halfway ties towards positive infinity.
    return std::clamp(static_cast<int>(std::floor(double(value)*32768+.5)),-32768,32767);
  };
  const auto predicted=encode(pcm,std::array<int,2>{sample(pcm[111]),sample(pcm[110])});
  const auto base=scoreLoop(independent,pcm),candidate=scoreLoop(predicted,pcm);
  if(!base)throw std::runtime_error("SPU waveform: independent encoding exceeded int16 range");
  return candidate&&*candidate<*base?predicted:independent;
}

} // namespace tetorica
