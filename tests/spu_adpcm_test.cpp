// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuAdpcm.h"
#include "SpuInstrument.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
namespace {thread_local bool tracking=false;thread_local unsigned allocations=0,frees=0;
void* allocate(std::size_t n){if(tracking)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void release(void* p)noexcept{if(tracking&&p)++frees;std::free(p);}}
void* operator new(std::size_t n){return allocate(n);}void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}void operator delete[](void* p,std::size_t)noexcept{release(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace tetorica;
namespace {
struct Stereo{std::vector<float> l,r;explicit Stereo(int n):l(n),r(n){}};
void render(SpuInstrument& synth,Stereo& out,int quantum=257){for(int at=0;at<static_cast<int>(out.l.size());){const int n=std::min(quantum,static_cast<int>(out.l.size())-at);synth.render(out.l.data()+at,out.r.data()+at,n);at+=n;}CHECK(synth.healthy());for(const auto* c:{&out.l,&out.r})for(float v:*c)CHECK(std::isfinite(v)&&std::abs(v)<=1);}
float peak(const std::vector<float>& v){float result=0;for(float x:v)result=std::max(result,std::abs(x));return result;}
template<class F>void rejects(F fn){bool rejected=false;try{fn();}catch(const std::exception&){rejected=true;}CHECK(rejected);}
RawAdpcmSample sine(){RawAdpcmSample s;const auto bytes=encodePcmCycle(makePcmCycle(WaveformShape::Sine));s.adpcm.assign(bytes.begin(),bytes.end());s.loopStartSamples=0;s.envelope={0x000f,0x3fc0};s.name="Original sine 音";s.reverb=true;return s;}
void validation(){
  const auto good=sine();validateRawAdpcmSample(good);CHECK(prepareRawAdpcmSample(good).adpcm==good.adpcm);
  for(unsigned field=0;field<11;++field){auto bad=good;
    if(field==0)bad.adpcm.clear();if(field==1)bad.adpcm.push_back(0);if(field==2)bad.adpcm.resize(vab::kMaxSampleBytes+16);
    if(field==3)bad.adpcm[0]=0x50;if(field==4)bad.adpcm[0]=0x0d;if(field==5)bad.adpcm[1]|=8;
    if(field==6)bad.adpcm[17]=1;if(field==7)bad.adpcm[49]=0;if(field==8)bad.loopStartSamples.reset();
    if(field==9)bad.loopStartSamples=1;if(field==10)bad.loopStartSamples=112;
    rejects([&]{validateRawAdpcmSample(bad);});rejects([&]{SpuInstrument core(bad);});
  }
  for(double value:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto bad=good;bad.baseFrequencyHz=value;rejects([&]{validateRawAdpcmSample(bad);});}
  for(double value:{-1.01,1.01,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){auto bad=good;bad.volumeLeft=value;rejects([&]{validateRawAdpcmSample(bad);});bad=good;bad.volumeRight=value;rejects([&]{validateRawAdpcmSample(bad);});}
  for(auto name:{std::string(321,'a'),std::string("x\0y",3),std::string("\xc0\x80",2),std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4)}){auto bad=good;bad.name=name;rejects([&]{validateRawAdpcmSample(bad);});}
  auto bad=good;bad.inspectorPitch=16384;rejects([&]{validateRawAdpcmSample(bad);});
  auto shot=good;shot.adpcm[1]=0;shot.adpcm[49]=1;shot.loopStartSamples.reset();validateRawAdpcmSample(shot);
  bad=shot;bad.adpcm[1]=4;rejects([&]{validateRawAdpcmSample(bad);});
  auto unsupported=good;unsupported.adpcm[1]=0;unsupported.adpcm[17]=4;unsupported.loopStartSamples=28;
  validateRawAdpcmSample(unsupported);rejects([&]{SpuInstrument core(unsupported);});
  unsupported.adpcm[17]=6;SpuInstrument allowed(unsupported);allowed.noteOn(69,127);Stereo out(8192);render(allowed,out);CHECK(peak(out.l)>.005f);
  auto changed=good;changed.inspectorPitch=0;CHECK(!sameRawAdpcmSample(good,changed));changed=good;changed.volumeLeft=-0.;auto zero=changed;zero.volumeLeft=0.;CHECK(!sameRawAdpcmSample(changed,zero));
}
void authoredWaveforms(){
  auto sample=sine();sample.waveform=makePcmCycle(WaveformShape::Sine);validateRawAdpcmSample(sample);
  // Independent Web encodePs1Adpcm(Float32(.8*sin), loop0), retained verbatim.
  constexpr std::array<std::uint8_t,64> legacy{{0,6,0,17,33,34,51,67,68,85,85,101,102,102,102,102,36,0,231,32,32,1,2,32,31,16,31,15,240,240,240,255,38,0,189,172,172,171,154,155,169,169,168,153,154,154,170,186,38,3,170,188,188,205,221,253,13,254,241,17,17,35,66,67}};
  sample.adpcm.assign(legacy.begin(),legacy.end());validateRawAdpcmSample(sample);CHECK(sample.adpcm==std::vector<std::uint8_t>(legacy.begin(),legacy.end()));
  auto bad=sample;(*bad.waveform)[0]=.5;rejects([&]{validateRawAdpcmSample(bad);});
  bad=sample;bad.baseFrequencyHz=440;rejects([&]{validateRawAdpcmSample(bad);});
  bad=sample;bad.adpcm[2]^=1;rejects([&]{validateRawAdpcmSample(bad);});
}
void signedVelocityAndMetadata(){
  float loud=0;
  for(int velocity:{127,64,1}){
    auto source=sine();source.volumeLeft=-.75;source.volumeRight=.25;source.inspectorPitch=1;
    SpuInstrument negative(source),positive(prepareRawAdpcmSample(source));negative.noteOn(69,velocity);positive.noteOn(69,velocity);
    Stereo a(4096),b(4096);render(negative,a);render(positive,b);
    CHECK(a.r==b.r&&peak(a.l)>0);for(unsigned i=0;i<a.l.size();++i)CHECK(std::abs(a.l[i]+b.l[i])<.00002f);
    if(velocity==127)loud=peak(a.l);if(velocity==64)CHECK(peak(a.l)>loud*.49f&&peak(a.l)<loud*.52f);
    CHECK(negative.sourceEnvelope().adsr2==source.envelope.adsr2&&negative.sourceReverbSend());
    source.inspectorPitch=16383;SpuInstrument ignored(source);ignored.noteOn(69,velocity);render(ignored,b);CHECK(a.l==b.l&&a.r==b.r);
  }
  auto source=sine();SpuInstrument owned(source),copy(source);source.adpcm.assign(source.adpcm.size(),0);owned.noteOn(69,127);copy.noteOn(69,127);Stereo a(4096),b(4096);render(owned,a);render(copy,b);CHECK(a.l==b.l&&a.r==b.r);
  for(double root:{std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()}){auto edge=sine();edge.baseFrequencyHz=root;SpuInstrument core(edge);core.noteOn(127,127);render(core,a);CHECK(core.healthy());}
}
void fullRamAndCallbackMemory(){
  auto source=sine();source.volumeLeft=-.75;source.volumeRight=-.25;source.adpcm[1]=0;source.adpcm[17]=6;source.loopStartSamples=28;
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a(source),b(source);Stereo whole(1031),parts(1031);allocations=frees=0;tracking=true;bool ok=a.prepare(rate)&&b.prepare(rate);
    for(unsigned stage=0;stage<64;++stage){for(auto* core:{&a,&b}){
      ok=core->setNoise({stage/8%3==1,stage%64})&&ok;PmonSettings pm;pm.enabled=stage/8%3==2;pm.depth=(stage%11)/10.;ok=core->setPmon(pm)&&ok;
      ok=core->setMorph({stage/4%2==1,(stage%17)/16.})&&ok;core->noteOn(stage%2?0:127,stage%3?64:1,stage%16);core->setPitchBend((stage*797)%16384,stage%16);
      core->setSustain(stage%4!=0,stage%16);if(stage%3==0)core->noteOff(stage%2?0:127,stage%16);if(stage%13==0)core->panic();}
      render(a,whole,1031);render(b,parts,7);CHECK(whole.l==parts.l&&whole.r==parts.r);
    }
    a.panic();b.panic();render(a,whole);render(b,parts);tracking=false;CHECK(ok&&allocations==0&&frees==0&&peak(whole.l)==0);
  }
  auto full=sine();full.adpcm.resize(vab::kMaxSampleBytes,0);full.adpcm[1]=0;full.adpcm[49]=0;full.adpcm[full.adpcm.size()-15]=1;full.loopStartSamples.reset();
  SpuInstrument maximum(full);CHECK(maximum.setMorph({true,.5}));maximum.noteOn(69,127);Stereo out(8192);render(maximum,out);CHECK(peak(out.l)>.005f);
}
}
int main(){validation();authoredWaveforms();signedVelocityAndMetadata();fullRamAndCallbackMemory();std::puts("PASS arbitrary raw ADPCM/tuning/loops/source ADSR/send/signed stereo velocity127/64/1; canonical+legacy authored PCM; inspector pitch metadata; fullRAM; partition; callback C++ new/delete0");}
