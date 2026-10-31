// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "VabBank.h"
#include <tetorica/ps1_spu.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
namespace {
thread_local bool tracking=false;
thread_local unsigned allocations=0,frees=0;
void* allocate(std::size_t n){if(tracking)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void release(void* p)noexcept{if(tracking&&p)++frees;std::free(p);}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace tetorica;
namespace {
struct Stereo{std::vector<float> l,r;explicit Stereo(int n):l(n),r(n){}};
void render(SpuInstrument& synth,Stereo& out,int quantum=257){
  for(int at=0;at<static_cast<int>(out.l.size());){const int n=std::min(quantum,static_cast<int>(out.l.size())-at);
    synth.render(out.l.data()+at,out.r.data()+at,n);at+=n;}
  CHECK(synth.healthy());for(const auto* c:{&out.l,&out.r})for(float v:*c)CHECK(std::isfinite(v)&&std::abs(v)<=1);
}
float peak(const Stereo& out,int begin=0){float value=0;for(int i=begin;i<static_cast<int>(out.l.size());++i)value=std::max(value,std::abs(out.l[i]));return value;}
bool same(const Stereo& a,const Stereo& b){return a.l==b.l&&a.r==b.r;}
template<class F> void rejects(F fn){bool rejected=false;try{fn();}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
PmonSettings hold(){PmonSettings s;s.enabled=true;s.decayMs=0;s.waveform=WaveformShape::Square;return s;}
vab::PreparedTone markerless(){
  const auto bytes=encodePcmCycle(makePcmCycle(WaveformShape::Sine));vab::PreparedTone tone;
  tone.adpcm.assign(bytes.begin(),bytes.end());tone.adpcm[1]=2;tone.decodedFrames=112;tone.loopStartSamples=0;
  tone.rootFrequencyHz=110;tone.adsr1=0x000f;tone.adsr2=0x1fdf;
  tone.volumeLeft=.25;tone.volumeRight=.75;tone.reverb=true;return tone;
}
void testPureBlend(){
  MorphEndpoints endpoints;validateMorphEndpoints(endpoints);
  CHECK(blendMorph(endpoints,0)==endpoints.a&&blendMorph(endpoints,1)==endpoints.b);
  endpoints.a.fill(.25f);endpoints.b.fill(.75f);CHECK(blendMorph(endpoints,.5)[0]==.5f);
  endpoints.a[0]=-0.f;CHECK(std::signbit(blendMorph(endpoints,0)[0]));
  for(double bad:{-1.,1.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})rejects([&]{blendMorph(endpoints,bad);});
  for(float bad:{-1.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
    auto invalid=endpoints;invalid.b[111]=bad;rejects([&]{validateMorphEndpoints(invalid);});rejects([&]{blendMorph(invalid,0);});
  }
  endpoints.a.fill(-1);endpoints.b.fill(1);for(double amount:{0.,.001,.25,.5,.75,.999,1.})
    for(float value:blendMorph(endpoints,amount))CHECK(value>=-1&&value<=1);
}
void testNativeCarrierOracle(){
  const MorphEndpoints endpoints;
  const auto mixed=blendMorph(endpoints,.375);std::array<std::int16_t,112> pcm{},square{};
  for(unsigned i=0;i<112;++i){pcm[i]=static_cast<std::int16_t>(std::floor(double(mixed[i])*32768+.5));square[i]=i<56?15565:-15565;}
  for(bool paired:{false,true}){
    SpuInstrument synth;CHECK(synth.setMorphEndpoints(endpoints));if(paired)CHECK(synth.setPmon(hold()));CHECK(synth.setMorph({true,.375}));synth.noteOn(69,127);
    void* native=ps1_spu_create();CHECK(native);CHECK(ps1_spu_set_mix_headroom(native,4));
    CHECK(ps1_spu_set_host_pcm_bank(native,0,square.data(),112,1)&&ps1_spu_set_host_pcm_bank(native,1,pcm.data(),112,1));
    CHECK(ps1_spu_set_host_masks(native,paired?1:0,paired?2:1));
    for(auto pair:{std::array<unsigned,2>{0x180,0x3fff},{0x182,0x3fff},{0x1aa,0xc000},{0x190,paired?2u:0u}})
      CHECK(ps1_spu_write(native,pair[0],static_cast<std::uint16_t>(pair[1])));
    const unsigned pitch=static_cast<unsigned>(std::lround(4096*440/(44100./112)));
    for(unsigned v=0;v<(paired?2u:1u);++v){const bool carrier=!paired||v==1;
      for(auto pair:{std::array<unsigned,2>{0,carrier?0x0c00u:0u},{2,carrier?0x0c00u:0u},{4,pitch},{8,15},{10,carrier?0x1fc0u:0x5fc0u}})
        CHECK(ps1_spu_write(native,v*16+pair[0],static_cast<std::uint16_t>(pair[1])));
      CHECK(ps1_spu_audition_key(native,v,1));
    }
    Stereo actual(8193),expected(8193);render(synth,actual,7);
    CHECK(ps1_spu_render(native,expected.l.data(),expected.r.data(),expected.l.size()));
    CHECK(same(actual,expected)&&peak(actual)>.005f);ps1_spu_destroy(native);
  }
}
void testOffAndLiveAmount(){
  SpuInstrument a,b;Stereo out(4096),reference(4096);a.noteOn(69,127);b.noteOn(69,127);
  CHECK(a.setMorph({false,.75}));render(a,out);render(b,reference);CHECK(same(out,reference));
  CHECK(a.setMorph({true,0})&&b.setMorph({true,0}));CHECK(a.activeVoiceCount()==0);
  a.noteOn(69,127);b.noteOn(69,127);render(a,out);render(b,reference);CHECK(same(out,reference));
  const auto frame=a.nativeFramesRendered();CHECK(a.setMorph({true,1}));CHECK(a.activeVoiceCount()==1&&a.nativeFramesRendered()==frame);
  render(a,out);render(b,reference);CHECK(out.l.front()!=0&&!same(out,reference)&&peak(out)>.005f);
  // A live edit preserves the current envelope, including pedal-held/released tails.
  a.setEnvelope({0x000f,0x1fce});a.setSustain(true);a.noteOff(69);CHECK(a.activeVoiceCount()==0);
  CHECK(a.setMorph({true,.3}));render(a,out);CHECK(peak(out)>.005f);
  a.setSustain(false);render(a,out);CHECK(a.setMorph({true,.6}));render(a,out);CHECK(peak(out)>.001f);
  a.panic();a.panic();render(a,out);CHECK(peak(out)==0&&a.morph().enabled);
  CHECK(a.prepare(48000));a.noteOn(69,127);render(a,out);CHECK(peak(out)>.005f);
}
void testValidationOwnershipAndNoisePriority(){
  SpuInstrument a,b;MorphEndpoints endpoints;CHECK(a.setMorphEndpoints(endpoints)&&b.setMorphEndpoints(endpoints));
  const float original=endpoints.a[0];endpoints.a[0]=.9f;CHECK(a.morphEndpoints().a[0]==original);
  CHECK(a.setMorph({true,.25})&&b.setMorph({true,.25}));a.noteOn(69,127);b.noteOn(69,127);Stereo out(1031),reference(1031);render(a,out);render(b,reference);
  for(float invalid:{1.01f,-1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
    auto bad=a.morphEndpoints();bad.a[111]=invalid;CHECK(!a.setMorphEndpoints(bad));render(a,out);render(b,reference);CHECK(same(out,reference));
  }
  for(double invalid:{-1.,1.01,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){
    CHECK(!a.setMorph({false,invalid})&&!a.setMorph({true,invalid}));CHECK(a.morph().enabled&&a.morph().amount==.25&&a.activeVoiceCount()==1);
    render(a,out);render(b,reference);CHECK(same(out,reference));
  }
  CHECK(a.setNoise({true,48})&&b.setNoise({true,48}));a.noteOn(69,127);b.noteOn(69,127);render(a,out);render(b,reference);CHECK(same(out,reference));
  CHECK(a.setMorph({false,.8}));CHECK(a.activeVoiceCount()==1);render(a,out);render(b,reference);CHECK(same(out,reference));
  CHECK(a.setMorph({true,.1}));CHECK(a.activeVoiceCount()==1&&a.morph().enabled);render(a,out);render(b,reference);CHECK(same(out,reference));
}
void testOriginalSourceAndFullRam(){
  const auto tone=markerless();SpuInstrument a(tone),reference(tone);Stereo out(8192),expected(8192);
  CHECK(a.setMorph({true,.5}));CHECK(a.setPmon(hold()));a.noteOn(69,127);render(a,out);CHECK(peak(out)>.001f);
  CHECK(a.sourceReverbSend()&&a.sourceEnvelope().adsr2==tone.adsr2);
  CHECK(a.setPmon({})&&a.setMorph({false,.5}));a.panic();CHECK(a.prepare(44100));
  for(int i=0;i<4;++i){render(a,out);CHECK(peak(out)==0);}
  a.noteOn(69,127);reference.noteOn(69,127);render(a,out);render(reference,expected);CHECK(same(out,expected)&&peak(out,4096)>.005f);
  auto full=tone;full.adpcm.resize(vab::kMaxSampleBytes,0);full.adpcm[1]=0;full.adpcm[49]=0;full.adpcm[full.adpcm.size()-15]=1;
  full.loopStartSamples.reset();full.decodedFrames=static_cast<std::uint32_t>(full.adpcm.size()/16*28);
  SpuInstrument maximum(full);CHECK(maximum.setMorph({true,.5}));maximum.noteOn(69,127);Stereo held(44100);render(maximum,held);CHECK(peak(held,42000)>.001f);
}
void testPartitionAndCallbackMemory(){
  const auto tone=markerless();
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a(tone),b(tone);Stereo whole(1031),parts(1031);allocations=frees=0;tracking=true;
    bool valid=a.prepare(rate)&&b.prepare(rate);auto pmon=hold();
    for(auto* synth:{&a,&b}){valid=synth->setMorph({true,.5})&&valid;for(unsigned i=0;i<24;++i)synth->noteOn(60+i,127,i%16);}
    render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts)&&a.activeVoiceCount()==24);
    for(unsigned stage=0;stage<128;++stage){
      pmon.enabled=stage/16%2;pmon.waveform=static_cast<WaveformShape>(stage%4);pmon.ratio=stage%2?.01:16;pmon.depth=(stage%11)/10.;
      for(auto* synth:{&a,&b}){
        valid=synth->setNoise({stage/16%3==1,stage%64})&&valid;
        valid=synth->setPmon(pmon)&&valid;valid=synth->setMorph({stage/8%3!=0,(stage%17)/16.})&&valid;
        synth->setPitchBend((stage*797)%16384,stage%16);synth->setSustain(stage%4!=0,stage%16);
        synth->noteOn(stage%2?0:127,127,stage%16);if(stage%3==0)synth->noteOff(stage%2?0:127,stage%16);
        if(stage%11==0)synth->allNotesOff();if(stage%17==0)synth->resetControllers();if(stage%23==0)synth->panic();
      }
      render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts));
    }
    a.panic();b.panic();render(a,whole);render(b,parts);tracking=false;
    CHECK(valid&&allocations==0&&frees==0&&peak(whole)==0&&peak(parts)==0);
  }
}
}
int main(){testPureBlend();testNativeCarrierOracle();testOffAndLiveAmount();testValidationOwnershipAndNoisePriority();testOriginalSourceAndFullRam();testPartitionAndCallbackMemory();
  std::puts("PASS pure112float blend/endpointvalidation; actual Native carrier+PMON two-bank oracle; Off-original/live Amount/held-pedal-release; Noisepriority; source/fullRAM/repeat; partition; callback C++ new/delete0");}
