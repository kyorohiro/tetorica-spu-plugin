// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// RIFF framing and format fields follow Microsoft's RIFF / WAVEFORMATEX
// references. ADPCM quantization is a port of hello_ymfm's original BSD
// docs/js/ps1_adpcm_encoder.js, using the retained pSXAuthor/R. Belmont
// decoder's integer prediction. This file does not link or run that decoder.
#include "WavImport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace tetorica {
namespace {
constexpr double pi=3.14159265358979323846264338327950288;
constexpr std::uint32_t outputRate=44100,minRate=8000,maxRate=384000;
constexpr std::size_t maxFrames=vab::kMaxSampleBytes/16*28;
[[noreturn]] void fail(const char* message){throw std::invalid_argument(message);}
std::uint16_t u16(const std::uint8_t* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
std::uint32_t u32(const std::uint8_t* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
bool id(const std::uint8_t* p,const char* name){return std::memcmp(p,name,4)==0;}
struct Wave {
  const std::uint8_t* data=nullptr;
  std::size_t dataBytes=0,frames=0;
  unsigned format=0,channels=0,bits=0,align=0;
  std::uint32_t rate=0;
};
Wave read(vab::BytesView bytes) {
  if(!bytes.data||bytes.size<12||bytes.size>kMaxWavFileBytes)
    fail("WAV requires a complete RIFF file of at most 32 MiB");
  const auto* p=bytes.data;
  if(!id(p,"RIFF")||!id(p+8,"WAVE"))fail("Expected little-endian RIFF/WAVE (not AIFF, RF64 or RIFX)");
  if(u32(p+4)<4||std::uint64_t(u32(p+4))+8!=bytes.size)
    fail("WAV RIFF length does not match the complete file");
  Wave wave;bool haveFmt=false,haveData=false,haveFact=false;
  std::uint32_t factFrames=0;
  for(std::size_t at=12;at<bytes.size;) {
    if(bytes.size-at<8)fail("WAV has a truncated chunk header");
    const auto length=std::size_t(u32(p+at+4));const auto begin=at+8;
    if(length>bytes.size-begin)fail("WAV chunk exceeds the RIFF boundary");
    const auto end=begin+length;
    if((length&1u)&&end==bytes.size)fail("WAV chunk is missing its WORD padding byte");
    if(id(p+at,"fmt ")) {
      if(haveFmt)fail("WAV has duplicate fmt chunks");haveFmt=true;
      // Classic PCM/float headers with no format-specific extension. A
      // zero-length WAVEFORMATEX extension is accepted for either encoding.
      if((length!=16&&length!=18)||(length==18&&u16(p+begin+16)!=0))
        fail("WAV fmt must be a 16-byte header or an 18-byte header with cbSize zero");
      wave.format=u16(p+begin);wave.channels=u16(p+begin+2);wave.rate=u32(p+begin+4);
      wave.align=u16(p+begin+12);wave.bits=u16(p+begin+14);
      if(!((wave.format==1&&wave.bits==16)||(wave.format==3&&wave.bits==32)))
        fail("WAV supports PCM16 or IEEE Float32 only; extensible/compressed formats are not supported");
      if(wave.channels!=1&&wave.channels!=2)fail("WAV supports mono or stereo only");
      if(wave.rate<minRate||wave.rate>maxRate)fail("WAV sample rate must be 8000 to 384000 Hz");
      if(wave.align!=wave.channels*(wave.bits/8)||
         std::uint64_t(wave.rate)*wave.align!=u32(p+begin+8))
        fail("WAV block alignment or average byte rate is inconsistent");
    }else if(id(p+at,"data")) {
      if(haveData)fail("WAV has duplicate data chunks");haveData=true;
      wave.data=p+begin;wave.dataBytes=length;
    }else if(id(p+at,"fact")) {
      if(haveFact||length<4)fail("WAV fact chunk is duplicate or truncated");
      haveFact=true;factFrames=u32(p+begin);
    }
    at=end+(length&1u);
  }
  if(!haveFmt||!haveData||!wave.dataBytes||wave.dataBytes%wave.align)
    fail("WAV requires fmt and nonempty, frame-aligned data");
  wave.frames=wave.dataBytes/wave.align;
  if(haveFact&&factFrames!=wave.frames)fail("WAV fact sample count does not match its data frames");
  return wave;
}
std::vector<float> decodeMono(const Wave& wave) {
  std::vector<float> mono(wave.frames);
  static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"WAV Float32 requires IEEE binary32");
  for(std::size_t frame=0;frame<wave.frames;++frame) {
    double value=0;
    for(unsigned channel=0;channel<wave.channels;++channel) {
      const auto* p=wave.data+frame*wave.align+channel*(wave.bits/8);
      if(wave.format==1) {
        const unsigned word=u16(p);const int sample=word<32768?static_cast<int>(word):static_cast<int>(word)-65536;
        value+=double(sample)/32768;
      }else {
        const auto word=u32(p);float sample;std::memcpy(&sample,&word,4);
        if(!std::isfinite(sample)||sample< -1||sample>1)fail("WAV Float32 samples must be finite and in [-1,1]; normalization is not automatic");
        value+=sample;
      }
    }
    mono[frame]=static_cast<float>(value/wave.channels);
  }
  return mono;
}
std::vector<float> resample(const std::vector<float>& input,std::uint32_t rate,std::size_t frames) {
  if(rate==outputRate)return input;
  // A normalized Blackman-windowed sinc, with its support scaled when
  // downsampling. The .95 cutoff leaves a transition band below Nyquist.
  // Nearest 1/1024 phase bounds work independently of file duration. Edge
  // extension keeps constant signals constant without inventing a fade.
  constexpr unsigned phases=1024;
  const double scale=std::max(1.,double(rate)/outputRate);
  const int radius=static_cast<int>(std::ceil(32*scale)),taps=radius*2+1;
  const double cutoff=.95/scale;
  std::vector<double> table(std::size_t(phases)*taps);
  for(unsigned phase=0;phase<phases;++phase) {
    double sum=0;
    for(int tap=0;tap<taps;++tap) {
      const double distance=tap-radius-double(phase)/phases;
      const double x=distance/radius;
      const double window=std::abs(x)>=1?0:.42+.5*std::cos(pi*x)+.08*std::cos(2*pi*x);
      const double weight=(std::abs(distance)<1e-12?cutoff:std::sin(pi*cutoff*distance)/(pi*distance))*window;
      table[std::size_t(phase)*taps+tap]=weight;sum+=weight;
    }
    for(int tap=0;tap<taps;++tap)table[std::size_t(phase)*taps+tap]/=sum;
  }
  std::vector<float> output(frames);
  for(std::size_t frame=0;frame<frames;++frame) {
    const std::uint64_t position=std::uint64_t(frame)*rate;
    auto center=static_cast<std::int64_t>(position/outputRate);
    auto phase=static_cast<unsigned>(((position%outputRate)*phases+outputRate/2)/outputRate);
    if(phase==phases){phase=0;++center;}
    const auto* weights=table.data()+std::size_t(phase)*taps;double sample=0;
    for(int tap=0;tap<taps;++tap) {
      const auto at=static_cast<std::size_t>(std::clamp<std::int64_t>(center+tap-radius,0,static_cast<std::int64_t>(input.size()-1)));
      sample+=double(input[at])*weights[tap];
    }
    output[frame]=static_cast<float>(sample);
  }
  return output;
}
int predict(int p1,int p2,int filter) {
  constexpr int coefficients[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
  const int sum=p1*coefficients[filter][0]+p2*coefficients[filter][1]+32;
  return sum>=0?sum/64:-((-sum+63)/64);
}
std::vector<std::uint8_t> encode(const std::vector<float>& pcm,bool loop) {
  const auto blocks=(pcm.size()+27)/28;std::vector<std::uint8_t> result(blocks*16);
  std::array<double,28> targets{};std::array<int,28> nibbles{},samples{},bestNibbles{},bestSamples{};
  int history1=0,history2=0;
  for(std::size_t block=0;block<blocks;++block) {
    for(unsigned n=0;n<28;++n)targets[n]=block*28+n<pcm.size()?double(pcm[block*28+n])*32768:0;
    double bestError=std::numeric_limits<double>::infinity();int bestFilter=0,bestShift=0;
    // Filter zero at the beginning is independent of the prior loop history.
    for(int filter=0;filter<=(block?4:0);++filter) {
      for(int shift=0;shift<=12;++shift) {
        const int step=4096>>shift;int p1=history1,p2=history2;double error=0;
        for(unsigned n=0;n<28;++n) {
          const int prediction=predict(p1,p2,filter);
          const int low=std::max(-8,static_cast<int>(std::ceil(double(-32768-prediction)/step)));
          const int high=std::min(7,static_cast<int>(std::floor(double(32767-prediction)/step)));
          if(low>high){error=std::numeric_limits<double>::infinity();break;}
          const int nibble=std::clamp(static_cast<int>(std::ceil((targets[n]-prediction)/step-.5)),low,high);
          const int sample=prediction+nibble*step;const double difference=targets[n]-sample;
          nibbles[n]=nibble;samples[n]=sample;p2=p1;p1=sample;error+=difference*difference;
          if(error>=bestError)break;
        }
        if(error<bestError){bestError=error;bestFilter=filter;bestShift=shift;bestNibbles=nibbles;bestSamples=samples;}
        if(bestError==0)break;
      }
      if(bestError==0)break;
    }
    const auto at=block*16;result[at]=static_cast<std::uint8_t>((bestFilter<<4)|bestShift);
    result[at+1]=static_cast<std::uint8_t>((block+1==blocks?(loop?3:1):0)|(loop&&block==0?6:0));
    for(unsigned n=0;n<28;n+=2)result[at+2+n/2]=static_cast<std::uint8_t>((bestNibbles[n]&15)|((bestNibbles[n+1]&15)<<4));
    history1=bestSamples[27];history2=bestSamples[26];
  }
  return result;
}
}
WavImportResult importWav(vab::BytesView bytes,std::string name,WavImportOptions options) {
  if(options.rootMidiNote<0||options.rootMidiNote>127)fail("WAV root MIDI note must be 0 to 127");
  const auto wave=read(bytes);
  const auto frames64=(std::uint64_t(wave.frames)*outputRate+wave.rate-1)/wave.rate;
  if(frames64==0||frames64>maxFrames)fail("WAV exceeds free SPU sample RAM after resampling; input is not truncated");
  const auto frames=static_cast<std::size_t>(frames64),padded=(frames+27)/28*28;
  if(padded>maxFrames)fail("WAV packet padding exceeds free SPU sample RAM; input is not truncated");
  WavImportResult result;result.sampleRate=wave.rate;result.channels=wave.channels;
  result.sourceFrames=wave.frames;result.resampledFrames=frames;result.paddedFrames=padded-frames;
  result.sample.name=std::move(name);result.sample.baseFrequencyHz=440*std::pow(2.,double(options.rootMidiNote-69)/12);
  // Validate name/tuning before resampling or encoding. All later state is
  // local; exceptions cannot mutate an existing source or callback handle.
  result.sample.adpcm.resize(16);result.sample.adpcm[1]=1;validateRawAdpcmSample(result.sample);
  const auto mono=decodeMono(wave);const auto converted=resample(mono,wave.rate,frames);
  result.sample.adpcm=encode(converted,options.loop);
  if(options.loop)result.sample.loopStartSamples=0;
  validateRawAdpcmSample(result.sample);return result;
}
} // namespace tetorica
