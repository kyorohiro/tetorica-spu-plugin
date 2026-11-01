// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "SpuBuiltin.h"
#include "VabBank.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool tracking = false;
thread_local unsigned allocations = 0, releases = 0;
void* allocate(std::size_t size) {
  if (tracking) ++allocations;
  if (auto* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void release(void* value) noexcept {
  if (tracking && value) ++releases;
  std::free(value);
}
void* allocateAligned(std::size_t size, std::size_t alignment) {
  if (tracking) ++allocations;
#ifdef _WIN32
  if (auto* value = _aligned_malloc(size ? size : 1, alignment)) return value;
#else
  void* value = nullptr;
  if (posix_memalign(&value, alignment, size ? size : 1) == 0) return value;
#endif
  throw std::bad_alloc();
}
void releaseAligned(void* value) noexcept {
  if (tracking && value) ++releases;
#ifdef _WIN32
  _aligned_free(value);
#else
  std::free(value);
#endif
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { release(value); }
void operator delete[](void* value) noexcept { release(value); }
void operator delete(void* value, std::size_t) noexcept { release(value); }
void operator delete[](void* value, std::size_t) noexcept { release(value); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete(void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }


#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while (0)
using namespace tetorica;
namespace {
constexpr std::array<double,3> rates{44100.,48000.,96000.};
struct Stereo {
  std::vector<float> l,r;
  explicit Stereo(int frames):l(frames),r(frames){}
};
void render(SpuInstrument& core,Stereo& pcm,int block=257) {
  for(int at=0;at<int(pcm.l.size());){const auto n=std::min(block,int(pcm.l.size())-at);
    core.render(pcm.l.data()+at,pcm.r.data()+at,n);at+=n;}
  CHECK(core.healthy());
  for(unsigned i=0;i<pcm.l.size();++i)CHECK(std::isfinite(pcm.l[i])&&std::isfinite(pcm.r[i]));
}
bool same(const Stereo& a,const Stereo& b){return a.l==b.l&&a.r==b.r;}
unsigned factor(int note){return note<90?1:note<102?2:note<114?4:note<126?8:16;}
double expected(int note,int bend){
  const auto delta=bend-8192;const auto semitones=2.*delta/(delta<0?8192.:8191.);
  const auto root=44100./112*factor(note);
  const auto target=440*std::exp2((note-69+semitones)/12.);
  const auto pitch=std::lround(4096*target/root);
  CHECK(pitch>=1&&pitch<=16383);
  return pitch*root/4096;
}
double frequency(const Stereo& pcm,double rate){
  double first=-1,last=-1;unsigned count=0;
  for(unsigned i=4097;i<pcm.l.size();++i)if(pcm.l[i-1]<=0&&pcm.l[i]>0){
    const auto at=i-1-double(pcm.l[i-1])/(pcm.l[i]-pcm.l[i-1]);
    if(first<0)first=at;last=at;++count;
  }
  CHECK(count>=10);return (count-1)*rate/(last-first);
}
void checkFrequency(const Stereo& pcm,double rate,int note,int bend){
  const auto want=expected(note,bend),got=frequency(pcm,rate);
  if(std::abs(got-want)>std::max(.015,want*.0003)){
    std::fprintf(stderr,"pitch rate=%.0f note=%d bend=%d want=%.9f got=%.9f\n",rate,note,bend,want,got);CHECK(false);
  }
}
vab::PreparedTone original(){
  vab::PreparedTone tone;
  tone.adpcm.assign(kBuiltinSineAdpcm.begin(),kBuiltinSineAdpcm.end());
  tone.rootFrequencyHz=44100./112;tone.decodedFrames=112;tone.loopStartSamples=0;
  tone.adsr1=0x000f;tone.adsr2=0x1fc0;tone.volumeLeft=tone.volumeRight=1;return tone;
}
void allNotesAndBends(){
  for(auto rate:rates){
    SpuInstrument core(BuiltinSineMode::FullRange);
    for(int note=0;note<128;++note)for(int bend:{0,8192,16383}){
      CHECK(core.prepare(rate));core.setPitchBend(bend,note%16);core.noteOn(note,127,note%16);
      const auto frames=4096+int(rate*std::max(.3,13./expected(note,bend)));
      Stereo pcm(frames);render(core,pcm);checkFrequency(pcm,rate,note,bend);
    }
  }
}
void lowAndLegacyExact(){
  for(auto rate:rates){
    SpuInstrument legacy,wide(BuiltinSineMode::FullRange),reference(original());
    CHECK(legacy.prepare(rate)&&wide.prepare(rate)&&reference.prepare(rate));
    Stereo a(2049),b(2049),c(2049);
    for(int note=0;note<128;++note){
      legacy.panic();wide.panic();reference.panic();
      for(int bend:{0,8192,16383}){
        const auto channel=note%16;
        legacy.setPitchBend(bend,channel);wide.setPitchBend(bend,channel);reference.setPitchBend(bend,channel);
        legacy.noteOn(note,100,channel);wide.noteOn(note,100,channel);reference.noteOn(note,100,channel);
        render(legacy,a);render(wide,b);render(reference,c);
        CHECK(same(a,c));if(note<90)CHECK(same(a,b));
      }
    }
  }
}
void heldBendAndTopology(){
  for(auto rate:rates)for(int note:{89,90,91,92,101,102,113,114,125,126,127}){
    SpuInstrument core(BuiltinSineMode::FullRange);CHECK(core.prepare(rate));
    core.setEnvelope({0x000f,0x1fdf});core.noteOn(note,127,4);Stereo pcm(int(rate*.4)+4096);
    render(core,pcm);checkFrequency(pcm,rate,note,8192);
    core.setSustain(true,4);core.noteOff(note,4);CHECK(core.activeVoiceCount()==0);
    for(int bend:{16383,0,8192}){core.setPitchBend(bend,4);render(core,pcm);checkFrequency(pcm,rate,note,bend);}
    core.setSustain(false,4);core.setPitchBend(16383,4);render(core,pcm);checkFrequency(pcm,rate,note,16383); //Release retains the latched source too.
  }
  for(auto rate:rates){
    SpuInstrument wide(BuiltinSineMode::FullRange),legacy;wide.prepare(rate);legacy.prepare(rate);
    Stereo a(8192),b(8192);
    for(int mode=0;mode<3;++mode){
      if(mode==0){CHECK(wide.setNoise({true,48})&&legacy.setNoise({true,48}));}
      if(mode==1){CHECK(wide.setNoise({false,48})&&legacy.setNoise({false,48}));CHECK(wide.setMorph({true,.375})&&legacy.setMorph({true,.375}));}
      if(mode==2){CHECK(wide.setMorph({false,.375})&&legacy.setMorph({false,.375}));}
      wide.noteOn(127,127);legacy.noteOn(127,127);render(wide,a);render(legacy,b);
      if(mode<2)CHECK(same(a,b));else checkFrequency(a,rate,127,8192);
    }
    PmonSettings pmon;pmon.enabled=true;pmon.depth=0;CHECK(wide.setPmon(pmon));
    wide.noteOn(127,127);render(wide,a);checkFrequency(a,rate,127,8192);
    pmon.depth=.25;CHECK(wide.setPmon(pmon));render(wide,a);
    pmon.enabled=false;CHECK(wide.setPmon(pmon));wide.noteOn(127,127);render(wide,a);checkFrequency(a,rate,127,8192);
  }
}
void partitionsAndRealtime(){
  for(auto rate:rates){
    SpuInstrument whole(BuiltinSineMode::FullRange),parts(BuiltinSineMode::FullRange);
    whole.prepare(rate);parts.prepare(rate);Stereo a(3073),b(3073);
    for(int stage=0;stage<80;++stage){
      const int note=stage%5==0?69:std::array<int,5>{90,102,114,126,127}[stage%5];
      const auto channel=stage%16;
      for(auto* core:{&whole,&parts}){
        core->setPitchBend((stage*997)%16384,channel);core->setSustain(stage%4!=0,channel);
        core->noteOn(note,100,channel);
        if(stage%3==0)core->noteOff(note,channel);
        if(stage%19==0)core->allSoundOff(channel);
      }
      render(whole,a,3073);render(parts,b,7);CHECK(same(a,b));
    }
    // First use of every zone/physical voice, stolen/repeated notes, and all
    // topology switches must use only the constructor's prepared native caches.
    SpuInstrument core(BuiltinSineMode::FullRange);core.prepare(rate);
    std::array<float,2048> l{},r{};bool ok=true;
    allocations=releases=0;tracking=true;
    for(int topology=0;topology<4;++topology){
      if(topology==1)ok=core.setPmon({true,WaveformShape::Sine,1,.25,0})&&ok;
      if(topology==2){ok=core.setPmon({false,WaveformShape::Sine,1,.25,0})&&ok;ok=core.setMorph({true,.375})&&ok;}
      if(topology==3)ok=core.setMorph({false,.375})&&ok;
      for(int note:{69,90,102,114,126,127,69,127,90}){
        core.panic();for(int voice=0;voice<24;++voice)core.noteOn(note==127?127-voice%2:note+voice%2,127,voice/2);
        core.render(l.data(),r.data(),l.size());
        for(int channel=0;channel<16;++channel){core.setPitchBend(16383,channel);core.setSustain(true,channel);core.noteOff(note,channel);core.noteOff(note==127?126:note+1,channel);}
        core.render(l.data(),r.data(),l.size());core.allNotesOff();core.setSustain(false);
      }
    }
    ok=core.healthy()&&ok;tracking=false;CHECK(ok);CHECK(allocations==0&&releases==0);
  }
}
}
int main(){
 allNotesAndBends();lowAndLegacyExact();heldBendAndTopology();partitionsAndRealtime();
 std::puts("PASS builtin FullRange MIDI0..127/bend endpoints at44.1/48/96k; original legacy all128 and new low0..89 PCM exact; held/pedal/release bends; Noise/Morph masking and PMON carrier; partition/stolen/retrigger; first-zone callback ordinary+aligned new/delete0");
}
