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
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
namespace {
thread_local bool gTrack=false;
thread_local std::size_t gNew=0,gDelete=0;
void* allocate(std::size_t size) {
  if(gTrack)++gNew;
  if(auto* value=std::malloc(size?size:1))return value;
  throw std::bad_alloc();
}
void release(void* value)noexcept {if(gTrack&&value)++gDelete;std::free(value);}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using tetorica::SpuInstrument;
using tetorica::vab::PreparedTone;
namespace {
constexpr std::array<std::uint8_t,64> kSine{{
  38,6,68,85,85,102,117,118,102,119,119,118,103,103,87,102,
  38,0,70,70,69,52,36,35,18,17,0,224,224,222,222,220,
  38,0,188,188,171,171,170,154,154,169,153,169,153,170,169,155,
  38,3,172,187,204,204,237,221,255,14,240,2,33,34,51,67}};
PreparedTone sine(unsigned blocks=4,unsigned loopBlock=0) {
  CHECK(blocks>=4&&loopBlock<blocks);
  PreparedTone tone;
  tone.adpcm.resize(blocks*16);
  for(unsigned block=0;block<blocks;++block){
    std::copy_n(kSine.data()+(block%4)*16,16,tone.adpcm.data()+block*16);
    tone.adpcm[block*16+1]=block==loopBlock?6:block==blocks-1?3:0;
  }
  tone.rootFrequencyHz=44100./112;
  tone.decodedFrames=blocks*28;
  tone.loopStartSamples=loopBlock*28;
  tone.adsr1=15;tone.adsr2=0x1fc0;
  tone.volumeLeft=tone.volumeRight=1;
  return tone;
}
struct Stereo {
  std::vector<float> left,right;
  explicit Stereo(unsigned frames):left(frames),right(frames){}
};
void render(SpuInstrument& synth,Stereo& pcm,int block=257) {
  for(int at=0;at<static_cast<int>(pcm.left.size());){
    const auto n=std::min(block,static_cast<int>(pcm.left.size())-at);
    synth.render(pcm.left.data()+at,pcm.right.data()+at,n);at+=n;
  }
  CHECK(synth.healthy());
  for(std::size_t i=0;i<pcm.left.size();++i)
    CHECK(std::isfinite(pcm.left[i])&&std::isfinite(pcm.right[i])&&
          std::abs(pcm.left[i])<=1&&std::abs(pcm.right[i])<=1);
}
float peak(const std::vector<float>& values,std::size_t start=0) {
  float p=0;for(std::size_t i=start;i<values.size();++i)p=std::max(p,std::abs(values[i]));return p;
}
double crossings(const std::vector<float>& values,double rate) {
  unsigned count=0,first=0,last=0;
  for(unsigned i=4097;i<values.size();++i)if(values[i-1]<=0&&values[i]>0){if(!count)first=i;last=i;++count;}
  CHECK(count>10);return (count-1)*rate/(last-first);
}
template<class F>void rejects(F&& f,const char* text=nullptr){
  try{f();}catch(const std::exception& e){CHECK(!text||std::string(e.what()).find(text)!=std::string::npos);return;}
  CHECK(false);
}
std::vector<std::uint8_t> fixture(const char* name){
  std::ifstream input(std::string(VAB_FIXTURE_DIR)+"/"+name,std::ios::binary);
  CHECK(input.good());return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
void defaultByteIdentity(){
  auto tone=sine();SpuInstrument legacy,imported(tone);
  CHECK(legacy.prepare(48000)&&imported.prepare(48000));
  Stereo a(48013),b(48013);
  legacy.noteOn(60,110);legacy.noteOn(64,100);legacy.noteOn(67,90);
  imported.noteOn(60,110);imported.noteOn(64,100);imported.noteOn(67,90);
  render(legacy,a);render(imported,b,17);CHECK(a.left==b.left&&a.right==b.right);
  legacy.allNotesOff();imported.allNotesOff();
  render(legacy,a);render(imported,b,7);CHECK(a.left==b.left&&a.right==b.right);
}
void demoBank(){
  const auto bytes=fixture("spu-synth-demo.vab");
  const auto bank=tetorica::vab::parse({bytes.data(),bytes.size()});
  auto plain=tetorica::vab::prepareTone(bank,0),fine=tetorica::vab::prepareTone(bank,1);
  SpuInstrument a(plain),b(fine);CHECK(a.prepare(48000)&&b.prepare(48000));
  a.noteOn(60,127);b.noteOn(60,127);
  Stereo pa(48000),pb(48000);render(a,pa);render(b,pb);
  CHECK(peak(pa.left)>.01f);
  CHECK(std::abs(crossings(pa.left,48000)-44100./112)<.2);
  CHECK(std::abs(crossings(pb.left,48000)/crossings(pa.left,48000)-std::exp2(.5/12))<.001);
  CHECK(plain.adpcm==tetorica::vab::prepareTone(bank,0).adpcm);
  auto one=tetorica::vab::prepareTone(bank,2);
  SpuInstrument shot(one);CHECK(shot.prepare(44100));shot.noteOn(69,127);
  Stereo out(4096);render(shot,out);CHECK(peak(out.left)>0&&peak(out.left,1024)==0);
}
void metadataAndIsolation(){
  auto tone=sine();tone.volumeLeft=1;tone.volumeRight=.25;
  SpuInstrument synth(tone),reference(tone); // SDK owns uploaded bytes.
  std::fill(tone.adpcm.begin(),tone.adpcm.end(),0);
  CHECK(synth.prepare(96000)&&reference.prepare(96000));
  synth.noteOn(69,127);reference.noteOn(69,127);
  Stereo pcm(96000),other(96000);render(synth,pcm);render(reference,other,7);
  CHECK(pcm.left==other.left&&pcm.right==other.right);
  CHECK(peak(pcm.left)>.02f&&peak(pcm.right)>.005f&&peak(pcm.right)<.011f);
  synth.panic();render(synth,pcm);render(reference,other);
  CHECK(peak(pcm.left)==0&&peak(other.left)>.02f);
  auto infinite=sine();infinite.adsr2=0x1fdf;
  SpuInstrument release(infinite);CHECK(release.prepare(44100));release.noteOn(69,127);
  Stereo tail(8192);render(release,tail);release.noteOff(69);render(release,tail);
  CHECK(peak(tail.left,4096)>.02f); // Authored infinite RR is preserved.
  release.panic();render(release,tail);CHECK(peak(tail.left)==0);
  auto muted=sine();muted.volumeLeft=0;muted.reverb=true;
  SpuInstrument side(muted);CHECK(side.prepare(48000));side.noteOn(69,127);
  render(side,pcm);CHECK(peak(pcm.left)==0&&peak(pcm.right)>.02f);
}
void validation(){
  const auto valid=sine();
  auto bad=valid;bad.adpcm.clear();rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm.push_back(0);rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.decodedFrames++;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[0]=0x50;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[0]=0x0d;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[1]|=8;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[17]=1;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[49]=0;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.loopStartSamples.reset();rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.loopStartSamples=28;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.loopStartSamples=113;rejects([&]{SpuInstrument s(bad);});
  bad=valid;bad.adpcm[17]=6;rejects([&]{SpuInstrument s(bad);});
  for(double root:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
    bad=valid;bad.rootFrequencyHz=root;rejects([&]{SpuInstrument s(bad);});
  }
  for(double vol:{-1.,1.1,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
    bad=valid;bad.volumeLeft=vol;rejects([&]{SpuInstrument s(bad);});
    bad=valid;bad.volumeRight=vol;rejects([&]{SpuInstrument s(bad);});
  }
  for(double root:{std::numeric_limits<double>::denorm_min(),std::numeric_limits<double>::max()}){
    auto edge=valid;edge.rootFrequencyHz=root;SpuInstrument s(edge);
    Stereo out(1024);s.noteOn(127,127);render(s,out);CHECK(s.healthy());
  }
}
float rawLoopTail(unsigned flag){
  void* chip=ps1_spu_create();CHECK(chip);
  std::array<std::uint8_t,64> bytes{};
  for(unsigned b=0;b<4;++b){bytes[b*16]=4;bytes[b*16+1]=b==1?flag:b==3?3:0;std::fill_n(bytes.data()+b*16+2,14,0x35);}
  CHECK(ps1_spu_load_ram(chip,0x1000,bytes.data(),bytes.size()));
  for(auto pair:{std::array<unsigned,2>{0x180,0x3fff},{0x182,0x3fff},{0x1aa,0xc000},
                {0,0x2000},{2,0x2000},{4,0x1000},{6,0x200},{8,15},{10,0x1fc0},{14,0x202},{0x188,1}})
    CHECK(ps1_spu_write(chip,pair[0],static_cast<std::uint16_t>(pair[1])));
  Stereo out(1024);CHECK(ps1_spu_render(chip,out.left.data(),out.right.data(),1024));
  ps1_spu_destroy(chip);return peak(out.left,512);
}
void loopFlagCompatibility(){
  CHECK(rawLoopTail(0)==rawLoopTail(2)&&rawLoopTail(0)>.01f);
  CHECK(rawLoopTail(4)==rawLoopTail(6)&&rawLoopTail(4)>.01f);
  for(const unsigned flags:{0u,4u})for(const unsigned loopBlock:{0u,1u}) {
    auto tone=sine(4,loopBlock);tone.adpcm[loopBlock*16+1]=flags;
    const auto original=tone.adpcm;auto canonical=tone;canonical.adpcm[loopBlock*16+1]=flags|2;
    SpuInstrument a(tone),b(canonical);CHECK(tone.adpcm==original);
    for(double rate:{44100.,48000.,96000.}){
      CHECK(a.prepare(rate)&&b.prepare(rate));a.noteOn(69,127);b.noteOn(69,127);
      Stereo actual(8193),expected(8193);render(a,actual);render(b,expected,7);
      CHECK(actual.left==expected.left&&actual.right==expected.right&&peak(actual.left,4096)>.005f);
      a.panic();b.panic();
    }
  }
}
void longSampleAndCallbackAllocations(){
  auto longTone=sine(4096,4092); //65536bytes,2.60s; END lies beyond old1024frameprime.
  longTone.adpcm[4092*16+1]=4;
  const auto original=longTone.adpcm;
  auto flag6=longTone;flag6.adpcm[4092*16+1]=6;
  SpuInstrument synth(longTone),reference(flag6);
  CHECK(longTone.adpcm==original);
  CHECK(synth.prepare(48000)&&reference.prepare(48000));
  Stereo a(160013),b(160013);
  synth.noteOn(69,127);reference.noteOn(69,127);
  render(synth,a,160013);render(reference,b,7);
  CHECK(a.left==b.left&&a.right==b.right&&peak(a.left,140000)>.02f);
  std::array<float,1024> left{},right{};
  gNew=gDelete=0;gTrack=true;
  bool ok=true;
  for(double rate:{44100.,48000.,96000.,8000.}){
    ok=synth.prepare(rate)&&ok;
    for(int ch=0;ch<16;++ch)synth.noteOn(69,127,ch);
    for(int ch=0;ch<8;++ch)synth.noteOn(68,127,ch);
    // Exercise actual END/loop callbacks for the long sample with all24voices.
    for(int i=0;i<160;++i)synth.render(left.data(),right.data(),1024);
    for(int i=0;i<96;++i){
      synth.noteOn(i%2?0:127,127,i%16);
      synth.render(left.data(),right.data(),1024);
      synth.noteOff(i%2?0:127,i%16);
      if(i%7==0)synth.panic();
    }
    synth.allNotesOff();
    for(int i=0;i<8;++i)synth.render(left.data(),right.data(),1024);
  }
  gTrack=false;CHECK(ok&&synth.healthy());CHECK(gNew==0&&gDelete==0);
  // Largest legal payload fits when placed at0x1000, with bounded offline prime.
  auto maximum=sine(static_cast<unsigned>(tetorica::vab::kMaxSampleBytes/16),4);
  SpuInstrument largest(maximum);largest.noteOn(127,127);Stereo out(4096);render(largest,out);
  maximum.adpcm.resize(maximum.adpcm.size()+16);maximum.decodedFrames+=28;
  rejects([&]{SpuInstrument s(maximum);});
}
}
int main(){
  defaultByteIdentity();demoBank();metadataAndIsolation();validation();
  loopFlagCompatibility();longSampleAndCallbackAllocations();
  std::puts("PASS: VAB Tone/native PCM, tuning, ADSR, pan, one-shot/long-loop, isolation, bounds, full RAM; flag4/markerless0 repeat PCM exact to flag6/2; original bytes; zero callback C++ new/delete after long cache prime");
}
