// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "SpuWaveform.h"
#include "fixtures/waveform/web_encoder_vectors.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
thread_local bool tracking=false;
thread_local unsigned allocations=0,frees=0;
void* allocate(std::size_t size){
  if(tracking)++allocations;
  if(void* p=std::malloc(size?size:1))return p;
  throw std::bad_alloc();
}
void release(void* p)noexcept{if(tracking&&p)++frees;std::free(p);}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}

#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace tetorica;
namespace {
struct Stereo{std::vector<float> l,r;explicit Stereo(int frames):l(frames),r(frames){}};
void render(SpuInstrument& instrument,Stereo& pcm,int quantum=257){
  for(int at=0;at<static_cast<int>(pcm.l.size());){
    const int count=std::min(quantum,static_cast<int>(pcm.l.size())-at);
    instrument.render(pcm.l.data()+at,pcm.r.data()+at,count);at+=count;
  }
  CHECK(instrument.healthy());
  for(const auto* channel:{&pcm.l,&pcm.r})for(float value:*channel)
    CHECK(std::isfinite(value)&&std::abs(value)<=1);
}
float peak(const Stereo& pcm,int start=0){
  float result=0;for(int i=start;i<static_cast<int>(pcm.l.size());++i)result=std::max(result,std::abs(pcm.l[i]));
  return result;
}
double frequency(const Stereo& pcm,double rate){
  int first=-1,last=-1,crossings=0;
  for(int i=1025;i<static_cast<int>(pcm.l.size());++i)if(pcm.l[i-1]<=0&&pcm.l[i]>0){
    if(first<0)first=i;last=i;++crossings;
  }
  CHECK(crossings>100);return (crossings-1)*rate/(last-first);
}
template<class F>void rejects(F action){
  bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}
  CHECK(rejected);
}
void testShapesAndValidation(){
  const auto sine=makePcmCycle(WaveformShape::Sine),triangle=makePcmCycle(WaveformShape::Triangle);
  const auto saw=makePcmCycle(WaveformShape::Saw),square=makePcmCycle(WaveformShape::Square);
  for(const auto* pcm:{&sine,&triangle,&saw,&square})validatePcmCycle(*pcm);
  CHECK(sine[0]==0&&sine[28]==.8f&&sine[84]==-.8f);
  CHECK(triangle[0]==0&&triangle[28]==.8f&&triangle[84]==-.8f);
  CHECK(saw[0]==-.8f&&saw[56]==0&&saw[111]>.78f);
  for(unsigned i=0;i<112;++i)CHECK(square[i]==(i<56?.8f:-.8f));
  rejects([]{makePcmCycle(static_cast<WaveformShape>(4));});
  rejects([]{makePcmCycle(static_cast<WaveformShape>(-1));});
  for(float invalid:{-1.001f,1.001f,std::numeric_limits<float>::infinity(),
      -std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
    auto pcm=sine;pcm[111]=invalid;
    rejects([&]{validatePcmCycle(pcm);});rejects([&]{encodePcmCycle(pcm);});
    rejects([&]{SpuInstrument instrument(pcm);});
  }
  PcmCycle extremes{};extremes.fill(-1);validatePcmCycle(extremes);encodePcmCycle(extremes);
  extremes.fill(1);validatePcmCycle(extremes);encodePcmCycle(extremes);
}
void testWebEncoderParity(){
  for(const auto& fixture:waveform_reference::vectors){
    PcmCycle pcm{};
    for(unsigned i=0;i<pcm.size();++i)std::memcpy(&pcm[i],&fixture.bits[i],sizeof(float));
    const auto encoded=encodePcmCycle(pcm);
    if(encoded!=fixture.adpcm)std::fprintf(stderr,"Web vector differs: %s\n",fixture.name);
    CHECK(encoded==fixture.adpcm&&encodePcmCycle(pcm)==encoded);
    CHECK(encoded[1]==6&&encoded[17]==0&&encoded[33]==0&&encoded[49]==3);
  }
  //The default independent fixture is retained verbatim in SpuInstrument.cpp.
  CHECK(encodePcmCycle(makePcmCycle(WaveformShape::Sine))==waveform_reference::vectors[0].adpcm);
}
void testSineCompatibilityAndOwnedInput(){
  for(double rate:{8000.,44100.,48000.,96000.,384000.}){
    SpuInstrument original,cycle(makePcmCycle(WaveformShape::Sine));
    CHECK(original.prepare(rate)&&cycle.prepare(rate));
    for(auto* instrument:{&original,&cycle}){
      instrument->noteOn(60,110,0);instrument->noteOn(64,110,1);instrument->noteOn(67,110,2);
    }
    Stereo a(12013),b(12013);render(original,a,12013);render(cycle,b,7);
    CHECK(a.l==b.l&&a.r==b.r);
    original.allNotesOff();cycle.allNotesOff();render(original,a);render(cycle,b);
    CHECK(a.l==b.l&&a.r==b.r&&peak(a,11000)==0);
  }
  auto pcm=makePcmCycle(WaveformShape::Triangle);SpuInstrument a(pcm),b(pcm);
  pcm.fill(0);a.noteOn(69,127);b.noteOn(69,127);Stereo out(8192),copy(8192);
  render(a,out);render(b,copy);CHECK(out.l==copy.l&&out.r==copy.r&&peak(out)>.02f);
  CHECK(a.sourceEnvelope().adsr1==0x000f&&a.sourceEnvelope().adsr2==0x1fc0&&!a.sourceReverbSend());
}
void testAudibleShapesPitchAndSilence(){
  std::vector<float> previous;
  for(auto shape:{WaveformShape::Sine,WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square}){
    SpuInstrument instrument(makePcmCycle(shape));CHECK(instrument.prepare(44100));
    instrument.noteOn(69,127);Stereo warm(4099),out(44100);render(instrument,warm);render(instrument,out);
    CHECK(peak(out)>.02f&&peak(out)<.05f&&std::abs(frequency(out,44100)-440)<.3);
    CHECK(out.l==out.r);if(!previous.empty())CHECK(out.l!=previous);previous=out.l;
    instrument.setPitchBend(16383);render(instrument,out);
    CHECK(std::abs(frequency(out,44100)-440*std::exp2(2./12))<.3);
    instrument.noteOff(69);render(instrument,out);CHECK(peak(out,2048)==0);
  }
  SpuInstrument silence(PcmCycle{});silence.noteOn(69,127);Stereo out(8192);
  render(silence,out);CHECK(peak(out)==0);
}
void testPartitionAndCallbackMemory(){
  for(auto shape:{WaveformShape::Sine,WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square})
    for(double rate:{8000.,44100.,48000.,96000.}){
      SpuInstrument a(makePcmCycle(shape)),b(makePcmCycle(shape));
      Stereo whole(1031),parts(1031);
      allocations=frees=0;tracking=true;
      const bool ready=a.prepare(rate)&&b.prepare(rate);
      for(int stage=0;stage<48;++stage){
        for(auto* instrument:{&a,&b}){
          const int note=stage%3==0?127:stage%3==1?0:40+stage;
          instrument->noteOn(note,127,stage%16);
          instrument->setPitchBend((stage*797)%16384,stage%16);
          instrument->setSustain(stage%4!=0,stage%16);
          if(stage%3==1)instrument->noteOff(note,stage%16);
          if(stage%7==0)instrument->allNotesOff(stage%16);
          if(stage%11==0)instrument->resetControllers();
          if(stage==31)instrument->panic();
        }
        render(a,whole,1031);render(b,parts,7);
        CHECK(whole.l==parts.l&&whole.r==parts.r);
      }
      a.panic();b.panic();render(a,whole);render(b,parts);
      tracking=false;
      CHECK(ready&&allocations==0&&frees==0&&peak(whole)==0&&peak(parts)==0);
    }
}
}
int main(){
  testShapesAndValidation();testWebEncoderParity();testSineCompatibilityAndOwnedInput();
  testAudibleShapesPitchAndSilence();testPartitionAndCallbackMemory();
  std::puts("PASS112-point waveform shapes/custom validation; 8 Web cyclic-ADPCM vectors byte-exact; default sine PCM exact; pitch/loops/release/partition; zero callback C++ new/delete");
}
