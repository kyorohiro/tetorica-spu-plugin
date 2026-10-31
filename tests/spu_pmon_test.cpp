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
bool same(const Stereo& a,const Stereo& b,int begin=0){return std::equal(a.l.begin()+begin,a.l.end(),b.l.begin()+begin)&&std::equal(a.r.begin()+begin,a.r.end(),b.r.begin()+begin);}
double sineResidual(const Stereo& out,int begin){
  // Least-squares440Hz sine fit with free phase. PMON integrates source phase,
  // so a finished modulator need not match the phase of an unmodulated note.
  double cc=0,ss=0,cs=0,yc=0,ys=0,energy=0;
  for(int i=begin;i<static_cast<int>(out.l.size());++i){
    const double angle=6.2831853071795864769*440*i/44100,c=std::cos(angle),s=std::sin(angle),y=out.l[i];
    cc+=c*c;ss+=s*s;cs+=c*s;yc+=y*c;ys+=y*s;energy+=y*y;
  }
  const double det=cc*ss-cs*cs,a=(yc*ss-ys*cs)/det,b=(ys*cc-yc*cs)/det;
  return std::max(0.,(energy-a*yc-b*ys)/energy);
}
PmonSettings hold(){PmonSettings s;s.enabled=true;s.decayMs=0;return s;}
vab::PreparedTone markerless(){
  const auto bytes=encodePcmCycle(makePcmCycle(WaveformShape::Sine));vab::PreparedTone tone;
  tone.adpcm.assign(bytes.begin(),bytes.end());tone.adpcm[1]=2;tone.decodedFrames=112;tone.loopStartSamples=0;
  tone.rootFrequencyHz=44100./112;tone.adsr1=0x000f;tone.adsr2=0x1fdf;
  tone.volumeLeft=.25;tone.volumeRight=.75;tone.reverb=true;return tone;
}
void testNativeOracleAndDepthZero(){
  SpuInstrument dry,zero;auto setting=hold();setting.depth=0;CHECK(zero.setPmon(setting));
  dry.noteOn(69,127);zero.noteOn(69,127);Stereo out(8193),reference(8193);
  render(dry,out);render(zero,reference,7);CHECK(same(out,reference)&&peak(out)>.02f);
  SpuInstrument paired;setting.waveform=WaveformShape::Square;setting.depth=.5;CHECK(paired.setPmon(setting));paired.noteOn(69,127);
  void* native=ps1_spu_create();CHECK(native);CHECK(ps1_spu_set_mix_headroom(native,4));
  const auto sine=encodePcmCycle(makePcmCycle(WaveformShape::Sine));
  CHECK(ps1_spu_load_ram(native,0x2000,sine.data(),sine.size()));
  std::array<std::int16_t,112> square{};for(unsigned i=0;i<112;++i)square[i]=i<56?15565:-15565;
  CHECK(ps1_spu_set_host_pcm(native,square.data(),112,1)&&ps1_spu_set_host_mask(native,1));
  for(auto pair:{std::array<unsigned,2>{0x180,0x3fff},{0x182,0x3fff},{0x1aa,0xc000},{0x190,2}})
    CHECK(ps1_spu_write(native,pair[0],static_cast<std::uint16_t>(pair[1])));
  const auto pitch=static_cast<unsigned>(std::lround(4096*440/(44100./112)));
  for(unsigned v=0;v<2;++v){
    for(auto pair:{std::array<unsigned,2>{0,v?0x0c00u:0u},{2,v?0x0c00u:0u},{4,pitch},{6,0x400},{8,15},{10,v?0x1fc0u:0x5fc0u},{14,0x400}})
      CHECK(ps1_spu_write(native,v*16+pair[0],static_cast<std::uint16_t>(pair[1])));
    CHECK(ps1_spu_audition_key(native,v,1));
  }
  CHECK(ps1_spu_render(native,reference.l.data(),reference.r.data(),reference.l.size()));
  render(paired,out,7);CHECK(same(out,reference)&&peak(out)>.02f);ps1_spu_destroy(native);
}
void testLiveEditsAndDecayLatching(){
  SpuInstrument a,b,dry;auto settings=hold();auto clean=settings;clean.depth=0;
  CHECK(a.setPmon(settings)&&b.setPmon(settings)&&dry.setPmon(clean));
  for(auto* synth:{&a,&b,&dry})synth->noteOn(69,127);
  Stereo out(8192),reference(8192),plain(8192);render(a,out);render(b,reference);render(dry,plain);CHECK(same(out,reference)&&!same(out,plain));
  settings.decayMs=13;const auto frame=a.nativeFramesRendered();CHECK(a.setPmon(settings));
  CHECK(a.activeVoiceCount()==1&&a.nativeFramesRendered()==frame);
  render(a,out);render(b,reference);render(dry,plain);CHECK(same(out,reference)); //Decay is next NoteOn only.
  for(auto* synth:{&a,&b,&dry})synth->noteOn(69,127);
  render(a,out);render(b,reference);render(dry,plain);
  CHECK(!same(out,reference,4096)&&sineResidual(out,4096)<.001&&sineResidual(reference,4096)>.01);
  settings.decayMs=0;CHECK(a.setPmon(settings)); //Still does not revive the already decayed modulator.
  render(a,out);render(dry,plain);CHECK(sineResidual(out,4096)<.001);
  a.noteOff(69);b.noteOff(69);Stereo settle(512);render(a,settle);render(b,settle);
  a.noteOn(69,127);b.noteOn(69,127);render(a,out);render(b,reference);CHECK(same(out,reference));
  settings.ratio=2;CHECK(a.setPmon(settings));
  render(a,out);render(b,reference);CHECK(out.l.front()==reference.l.front()&&!same(out,reference)&&peak(out)>.01f);
  for(auto shape:{WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square,WaveformShape::Sine}){
    settings.waveform=shape;settings.depth=settings.depth==.5?.9:.5;
    const auto before=a.nativeFramesRendered();CHECK(a.setPmon(settings));CHECK(a.activeVoiceCount()==1&&a.nativeFramesRendered()==before);
    render(a,out);CHECK(peak(out)>.01f);
  }
  a.setEnvelope({0x000f,0x1fce});a.noteOff(69);render(a,out);
  settings.ratio=.25;settings.depth=.95;CHECK(a.setPmon(settings));render(a,out);CHECK(peak(out)>.001f&&a.activeVoiceCount()==0);
}
void testChannelsPedalAndAllocator(){
  SpuInstrument a,b;CHECK(a.setPmon(hold())&&b.setPmon(hold()));
  a.noteOn(69,127,1);b.noteOn(69,127,1);Stereo out(4096),reference(4096);
  render(a,out);render(b,reference);a.setPitchBend(16383,0);a.setSustain(true,0);
  render(a,out);render(b,reference);CHECK(same(out,reference));
  a.setSustain(true,1);a.noteOff(69,1);CHECK(a.activeVoiceCount()==0);
  render(a,out);render(b,reference);CHECK(same(out,reference));
  a.resetControllers(1);render(a,out);CHECK(peak(out,2048)==0);
  b.allNotesOff();render(b,reference);CHECK(peak(reference,2048)==0);
  a.panic();CHECK(a.prepare(44100));for(int note=0;note<12;++note)a.noteOn(60+note,127,note%16);
  CHECK(a.activeVoiceCount()==12);a.noteOn(72,127,12);CHECK(a.activeVoiceCount()==12);
  a.noteOff(60,0);CHECK(a.activeVoiceCount()==12);a.noteOff(72,12);CHECK(a.activeVoiceCount()==11);
  a.noteOn(61,127,1);CHECK(a.activeVoiceCount()==11); //Existing held identity retriggers its pair.
  a.setSustain(true,1);a.allNotesOff();CHECK(a.activeVoiceCount()==0);render(a,out);
  a.resetControllers();render(a,out);CHECK(peak(out,2048)==0);a.panic();render(a,out);CHECK(peak(out)==0);
}
void testInvalidTuples(){
  SpuInstrument a,b;const auto settings=hold();CHECK(a.setPmon(settings)&&b.setPmon(settings));a.noteOn(69,127);b.noteOn(69,127);
  Stereo out(1031),reference(1031);render(a,out);render(b,reference);
  for(unsigned field=0;field<4;++field)for(double value:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),10000.}){
    auto bad=settings;if(field==0)bad.waveform=static_cast<WaveformShape>(99);if(field==1)bad.ratio=value;if(field==2)bad.depth=value;if(field==3)bad.decayMs=value;
    CHECK(!a.setPmon(bad));CHECK(a.pmon().enabled&&a.pmon().depth==settings.depth&&a.activeVoiceCount()==1);
    render(a,out);render(b,reference);CHECK(same(out,reference));
  }
}
void testSourceAndFullRamCompatibility(){
  const auto tone=markerless();SpuInstrument a(tone),reference(tone);
  a.setEnvelope({0x000f,0x1fdf});auto settings=hold();CHECK(a.setPmon(settings));a.noteOn(69,127);Stereo out(8192),expected(8192);render(a,out);CHECK(peak(out)>.005f);
  CHECK(a.setNoise({true,48})&&!a.pmon().enabled);CHECK(a.activeVoiceCount()==0);render(a,out);CHECK(peak(out)==0);
  a.noteOn(69,127);render(a,out);CHECK(peak(out)>.001f);
  CHECK(a.setPmon(settings)&&!a.noise().enabled);CHECK(a.activeVoiceCount()==0);a.noteOn(69,127);render(a,out);CHECK(peak(out)>.001f);
  settings.enabled=false;CHECK(a.setPmon(settings));a.panic();CHECK(a.prepare(44100));
  for(int i=0;i<4;++i){render(a,out);CHECK(peak(out)==0);}
  a.noteOn(69,127);reference.noteOn(69,127);render(a,out);render(reference,expected);CHECK(same(out,expected)&&peak(out,4096)>.005f);
  CHECK(a.sourceReverbSend()&&a.sourceEnvelope().adsr1==tone.adsr1&&a.sourceEnvelope().adsr2==tone.adsr2);
  auto full=tone;full.adpcm.resize(vab::kMaxSampleBytes,0);full.adpcm[1]=0;full.adpcm[49]=0;full.adpcm[full.adpcm.size()-15]=1;
  full.loopStartSamples.reset();full.decodedFrames=static_cast<std::uint32_t>(full.adpcm.size()/16*28);full.rootFrequencyHz=10;
  SpuInstrument maximum(full);settings.enabled=true;CHECK(maximum.setPmon(settings));maximum.noteOn(127,127);render(maximum,out);CHECK(peak(out)>0);
}
void testPartitionAndCallbackMemory(){
  const auto tone=markerless();
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a(tone),b(tone);Stereo whole(1031),parts(1031);auto settings=hold();
    allocations=frees=0;tracking=true;bool valid=a.prepare(rate)&&b.prepare(rate);
    for(auto* synth:{&a,&b}){valid=synth->setPmon(settings)&&valid;for(unsigned i=0;i<12;++i)synth->noteOn(60+i,127,i);}
    render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts)&&a.activeVoiceCount()==12);
    for(unsigned stage=0;stage<128;++stage){
      settings.waveform=static_cast<WaveformShape>(stage%4);settings.ratio=stage%2?.01:16;
      settings.depth=(stage%11)/10.;settings.decayMs=stage%2?0:5000;settings.enabled=stage/16%3==0;
      for(auto* synth:{&a,&b}){
        valid=synth->setNoise({stage/16%3==1,stage%64})&&valid;valid=synth->setPmon(settings)&&valid;
        synth->setPitchBend((stage*797)%16384,stage%16);synth->setSustain(stage%4!=0,stage%16);
        synth->noteOn(stage%2?0:127,127,stage%16);if(stage%3==0)synth->noteOff(stage%2?0:127,stage%16);
        if(stage%11==0)synth->allNotesOff();if(stage%17==0)synth->resetControllers();
        if(stage%23==0){synth->panic();synth->panic();}
      }
      render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts));
    }
    a.panic();b.panic();render(a,whole);render(b,parts);tracking=false;
    CHECK(valid&&allocations==0&&frees==0&&peak(whole)==0&&peak(parts)==0);
  }
}
}
int main(){
  testNativeOracleAndDepthZero();testLiveEditsAndDecayLatching();testChannelsPedalAndAllocator();
  testInvalidTuples();testSourceAndFullRamCompatibility();testPartitionAndCallbackMemory();
  std::puts("PASS native even-OUTX/odd-PMON oracle/depth-zero dry;12-pair voice/channel/pedal/steal; live waves/depth/ratio and latched decay; source/repeat/fullRAM; partition; zero callback C++ new/delete");
}
