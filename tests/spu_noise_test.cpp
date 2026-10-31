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
    const int n=std::min(quantum,static_cast<int>(pcm.l.size())-at);
    instrument.render(pcm.l.data()+at,pcm.r.data()+at,n);at+=n;
  }
  CHECK(instrument.healthy());
  for(const auto* channel:{&pcm.l,&pcm.r})for(float value:*channel)
    CHECK(std::isfinite(value)&&std::abs(value)<=1);
}
float peak(const Stereo& pcm,int start=0){
  float value=0;for(int i=start;i<static_cast<int>(pcm.l.size());++i)value=std::max(value,std::abs(pcm.l[i]));
  return value;
}
bool same(const Stereo& a,const Stereo& b){return a.l==b.l&&a.r==b.r;}
vab::PreparedTone oneShot(){
  const auto bytes=encodePcmCycle(makePcmCycle(WaveformShape::Sine));
  vab::PreparedTone tone;tone.adpcm.assign(bytes.begin(),bytes.end());
  tone.adpcm[1]=0;tone.adpcm[49]=1;tone.decodedFrames=112;
  tone.rootFrequencyHz=10;tone.adsr1=0x000f;tone.adsr2=0x1fc0;
  tone.volumeLeft=.25;tone.volumeRight=.75;tone.reverb=true;
  return tone;
}
void testNativeCaptureAndNoiseCarrier(){
  void* chip=ps1_spu_create();CHECK(chip);
  //The apparent0x800 prefix is a capture buffer, not free persistent RAM.
  std::array<std::uint8_t,16> packet{};packet[1]=7;
  CHECK(ps1_spu_load_ram(chip,0x800,packet.data(),packet.size()));
  std::array<float,8192> left{},right{};
  CHECK(ps1_spu_render(chip,left.data(),right.data(),1024));
  std::array<std::uint8_t,16> after{};
  CHECK(ps1_spu_read_ram(chip,0x800,after.data(),after.size()));CHECK(after!=packet);
  CHECK(std::all_of(after.begin(),after.end(),[](std::uint8_t v){return v==0;}));
  constexpr std::array<std::int16_t,28> zero{};
  CHECK(ps1_spu_set_host_pcm(chip,zero.data(),zero.size(),1)&&ps1_spu_set_host_mask(chip,1));
  for(const auto pair:{std::array<unsigned,2>{0x180,0x3fff},{0x182,0x3fff},{0x1aa,0xf000},
      {0,0x0c00},{2,0x0c00},{4,4096},{8,0x000f},{10,0x1fc0},{0x194,1},{0x188,1}})
    CHECK(ps1_spu_write(chip,pair[0],static_cast<std::uint16_t>(pair[1])));
  CHECK(ps1_spu_render(chip,left.data(),right.data(),left.size()));
  CHECK(*std::max_element(left.begin(),left.end())>.01f);
  CHECK(ps1_spu_write(chip,0x194,0)&&ps1_spu_write(chip,0x188,1));
  CHECK(ps1_spu_render(chip,left.data(),right.data(),left.size()));
  CHECK(std::all_of(left.begin(),left.end(),[](float v){return v==0;}));
  ps1_spu_destroy(chip); //The zero host source produces sound only through NOISE.
}
void testPitchIndependenceAndLiveClock(){
  SpuInstrument a,b;CHECK(a.prepare(44100)&&b.prepare(44100));
  CHECK(a.setNoise({true,48})&&b.setNoise({true,48}));
  a.noteOn(0,127);b.noteOn(127,127);Stereo out(8192),reference(8192);
  render(a,out);render(b,reference);CHECK(same(out,reference)&&peak(out)>.01f);
  a.setPitchBend(16383);render(a,out);render(b,reference);CHECK(same(out,reference));
  const auto frame=a.nativeFramesRendered();CHECK(a.setNoise({true,63}));
  CHECK(a.activeVoiceCount()==1&&a.nativeFramesRendered()==frame&&a.noise().clock==63);
  render(a,out);render(b,reference);
  CHECK(out.l.front()==reference.l.front()&&out.l!=reference.l&&peak(out)>.01f);
  a.noteOff(0);render(a,out);CHECK(peak(out,2048)==0);
  CHECK(b.activeVoiceCount()==1);render(b,reference);CHECK(peak(reference)>.01f);
}
void testClockValidationAndRelease(){
  SpuInstrument a,b;for(auto* instrument:{&a,&b}){
    CHECK(instrument->prepare(44100)&&instrument->setNoise({true,48}));instrument->noteOn(69,127);
  }
  Stereo out(4096),reference(4096);render(a,out);render(b,reference);
  for(auto invalid:{64u,0xffffffffu}){
    CHECK(!a.setNoise({true,invalid})&&!a.setNoise({false,invalid}));
    CHECK(a.noise().enabled&&a.noise().clock==48&&a.activeVoiceCount()==1&&a.healthy());
    render(a,out);render(b,reference);CHECK(same(out,reference));
  }
  for(auto* instrument:{&a,&b}){
    instrument->setEnvelope({0x000f,0x1fce});instrument->noteOff(69);
  }
  render(a,out);render(b,reference);CHECK(same(out,reference));
  const auto frame=a.nativeFramesRendered();CHECK(a.setNoise({true,0}));
  CHECK(a.activeVoiceCount()==0&&a.nativeFramesRendered()==frame);
  render(a,out);render(b,reference);
  CHECK(out.l.front()==reference.l.front()&&out.l!=reference.l&&peak(out)>.01f);
  a.panic();a.panic();render(a,out);CHECK(peak(out)==0&&a.noise().enabled&&a.noise().clock==0);
  a.noteOn(69,127);render(a,out);CHECK(peak(out)>.01f);
  CHECK(a.prepare(48000));render(a,out);CHECK(peak(out)==0&&a.noise().enabled);
}
void testPedalAndStoredClock(){
  SpuInstrument a,b;for(auto* instrument:{&a,&b}){
    CHECK(instrument->prepare(44100)&&instrument->setNoise({true,48}));
    instrument->setSustain(true,3);instrument->noteOn(69,127,3);
  }
  Stereo out(4096),reference(4096);render(a,out);render(b,reference);
  a.noteOff(69,3);b.noteOff(69,3);CHECK(a.activeVoiceCount()==0);
  CHECK(a.setNoise({true,60})&&b.setNoise({true,60}));
  render(a,out);render(b,reference);CHECK(same(out,reference)&&peak(out)>.01f);
  a.setSustain(false,3);render(a,out);CHECK(peak(out,2048)==0);
  render(b,reference);CHECK(peak(reference)>.01f);
  SpuInstrument sample,copy;sample.noteOn(69,127);copy.noteOn(69,127);
  render(sample,out);render(copy,reference);CHECK(sample.setNoise({false,17}));
  CHECK(sample.activeVoiceCount()==1&&sample.noise().clock==17);
  render(sample,out);render(copy,reference);CHECK(same(out,reference));
}
void testSourceSwitchAndOneShotSends(){
  const auto tone=oneShot();SpuInstrument switched(tone),noiseReference(tone),sampleReference(tone);
  for(auto* instrument:{&switched,&noiseReference,&sampleReference})CHECK(instrument->prepare(44100));
  switched.noteOn(127,127);Stereo shortSample(13);render(switched,shortSample);
  const ReverbSettings room{ReverbPreset::Room,true,true,.75,.75};
  CHECK(switched.setNoise({true,48})&&switched.activeVoiceCount()==0);
  CHECK(switched.setReverb(room)&&noiseReference.setReverb(room));
  CHECK(noiseReference.setNoise({true,48}));
  switched.noteOn(0,127);noiseReference.noteOn(127,127);
  Stereo out(8192),reference(8192);render(switched,out);render(noiseReference,reference);
  CHECK(same(out,reference)&&peak(out)>.001f); //Old one-shot END cannot erase the new send.
  Stereo held(44100);render(switched,held);CHECK(peak(held,42000)>.001f);
  CHECK(switched.sourceReverbSend()&&switched.sourceEnvelope().adsr1==tone.adsr1);
  CHECK(switched.setNoise({false,31}));render(switched,out);CHECK(peak(out)==0);
  //Panic/prepare preserve source-latch intent until the first new RAM Note On.
  switched.panic();CHECK(switched.prepare(44100));
  CHECK(sampleReference.setReverb(room));switched.noteOn(127,127);sampleReference.noteOn(127,127);
  render(switched,out);render(sampleReference,reference);CHECK(same(out,reference)&&peak(out)>.00001f);
  switched.noteOff(127);sampleReference.noteOff(127);render(switched,out);render(sampleReference,reference);
  CHECK(same(out,reference));
}
void testFullRamCompatibility(){
  auto tone=oneShot();tone.adpcm.resize(vab::kMaxSampleBytes,0);
  tone.adpcm[49]=0;tone.adpcm[tone.adpcm.size()-15]=1;
  tone.decodedFrames=static_cast<std::uint32_t>(tone.adpcm.size()/16*28);
  SpuInstrument instrument(tone);CHECK(instrument.prepare(44100));
  CHECK(instrument.setNoise({true,63}));instrument.noteOn(127,127);Stereo out(44100);
  render(instrument,out);CHECK(peak(out,42000)>.001f);
  CHECK(instrument.setNoise({false,63}));instrument.noteOn(127,127);render(instrument,out);
  CHECK(peak(out)>.00001f&&instrument.sourceReverbSend());
}
void testMarkerlessLoopRestoration(){
  auto tone=oneShot();tone.adpcm[1]=2;tone.adpcm[49]=3;
  tone.loopStartSamples=0;tone.rootFrequencyHz=44100./112;tone.adsr2=0x1fdf;
  SpuInstrument switched(tone),reference(tone);
  CHECK(switched.setNoise({true,48}));
  for(unsigned i=0;i<24;++i)switched.noteOn(69+i/16,127,i%16);
  Stereo out(8192),expected(8192);render(switched,out);CHECK(peak(out)>.01f);
  CHECK(switched.setNoise({false,48}));
  switched.panic();CHECK(switched.prepare(44100));
  // Muted old host cursors may still finish a release and rewrite repaddr.
  for(int i=0;i<4;++i){render(switched,out);CHECK(peak(out)==0);}
  for(unsigned i=0;i<24;++i){
    switched.noteOn(69+i/16,127,i%16);reference.noteOn(69+i/16,127,i%16);
  }
  render(switched,out);render(reference,expected);
  CHECK(same(out,expected)&&peak(out,4096)>.02f);
}
void testPartitionAndCallbackMemory(){
  const auto tone=oneShot();
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a(tone),b(tone);Stereo whole(1031),parts(1031);
    allocations=frees=0;tracking=true;bool valid=a.prepare(rate)&&b.prepare(rate);
    for(auto* instrument:{&a,&b}){
      valid=instrument->setNoise({true,48})&&valid;
      for(int note=0;note<24;++note)instrument->noteOn(note,127,note%16);
    }
    render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts)&&a.activeVoiceCount()==24);
    for(unsigned stage=0;stage<96;++stage){
      for(auto* instrument:{&a,&b}){
        valid=instrument->setNoise({stage/8%2==0,stage%64})&&valid;
        instrument->setPitchBend((stage*797)%16384,stage%16);
        instrument->setSustain(stage%4!=0,stage%16);
        instrument->noteOn(stage%128,127,stage%16);
        if(stage%3==1)instrument->noteOff(stage%128,stage%16);
        if(stage%11==0)instrument->allNotesOff();
        if(stage%17==0)instrument->resetControllers();
        if(stage%23==0){instrument->panic();instrument->panic();}
      }
      render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts));
    }
    a.panic();b.panic();render(a,whole);render(b,parts);tracking=false;
    CHECK(valid&&allocations==0&&frees==0&&peak(whole)==0&&peak(parts)==0);
  }
}
}
int main(){
  testNativeCaptureAndNoiseCarrier();testPitchIndependenceAndLiveClock();
  testClockValidationAndRelease();testPedalAndStoredClock();testSourceSwitchAndOneShotSends();
  testFullRamCompatibility();testMarkerlessLoopRestoration();testPartitionAndCallbackMemory();
  std::puts("PASS actual nativeNoise/liveclock/Note-bend independence; capture-prefix guard/private hostloop; held/release/pedal/source latch/one-shot-send/fullRAM; partition; zero callback C++ new/delete");
}
