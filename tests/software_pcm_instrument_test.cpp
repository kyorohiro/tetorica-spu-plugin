// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SoftwarePcm.h"
#include "SpuInstrument.h"
#include "SpuAdpcm.h"
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
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace tetorica;
namespace {
constexpr double pi=3.14159265358979323846264338327950288;
struct Stereo {std::vector<float> l,r;explicit Stereo(std::size_t n):l(n),r(n){}};
void render(SpuInstrument& synth,Stereo& pcm,int quantum=257){for(int at=0;at<static_cast<int>(pcm.l.size());){const int n=std::min(quantum,static_cast<int>(pcm.l.size())-at);synth.render(pcm.l.data()+at,pcm.r.data()+at,n);at+=n;}CHECK(synth.healthy());}
float peak(const std::vector<float>& pcm){float p=0;for(float v:pcm){CHECK(std::isfinite(v));p=std::max(p,std::abs(v));}return p;}
bool same(const Stereo& a,const Stereo& b){return a.l==b.l&&a.r==b.r;}
template<class F>void rejects(F&& f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}CHECK(failed);}
SoftwarePcmSample sine(std::size_t frames=112) {
  std::vector<std::int16_t> pcm(frames);for(std::size_t i=0;i<frames;++i)pcm[i]=static_cast<std::int16_t>(std::lround(12000*std::sin(2*pi*i/112)));
  SoftwarePcmSample source;source.pcm=SoftwarePcmBuffer::copyOf(pcm);source.baseFrequencyHz=44100./112;source.loop=true;source.name="Original software sine 音";return source;
}
double frequency(const std::vector<float>& pcm,double rate) {
  unsigned crossings=0;std::size_t first=0,last=0;
  for(std::size_t i=1001;i<pcm.size();++i)if(pcm[i-1]<=0&&pcm[i]>0){if(!crossings)first=i;last=i;++crossings;}
  CHECK(crossings>2);return double(crossings-1)*rate/double(last-first);
}
void playbackAndPitch() {
  auto source=sine();source.envelope.adsr2=0x3fc0;source.reverb=true;
  SpuInstrument core(source);CHECK(core.sourceEnvelope().adsr2==0x3fc0&&core.sourceReverbSend());
  Stereo pcm(44100);core.noteOn(69,127);render(core,pcm);CHECK(peak(pcm.l)>.002f&&std::abs(frequency(pcm.l,44100)-440)<1);
  core.setPitchBend(16383);render(core,pcm);CHECK(std::abs(frequency(pcm.l,44100)-440*std::exp2(2./12))<1);
  core.setPitchBend(0);render(core,pcm);CHECK(std::abs(frequency(pcm.l,44100)-440*std::exp2(-2./12))<1);
  core.setSustain(true);core.noteOff(69);CHECK(core.activeVoiceCount()==0);render(core,pcm);CHECK(peak(pcm.l)>.002);
  core.setSustain(false);render(core,pcm);render(core,pcm);CHECK(peak(pcm.l)==0);
  for(int velocity:{127,64,1}) {
    auto negative=source,positive=source;negative.volumeLeft=-.75;negative.volumeRight=-.25;positive.volumeLeft=.75;positive.volumeRight=.25;
    SpuInstrument a(negative),b(positive);a.noteOn(60,velocity);b.noteOn(60,velocity);Stereo x(4096),y(4096);render(a,x);render(b,y);
    CHECK(peak(x.l)>0);for(std::size_t i=0;i<x.l.size();++i){CHECK(std::abs(x.l[i]+y.l[i])<.00002f);CHECK(std::abs(x.r[i]+y.r[i])<.00002f);}
  }
  auto ignored=source;ignored.inspectorPitch=0;SpuInstrument a(source),b(ignored);a.noteOn(60,127);b.noteOn(60,127);Stereo x(4096),y(4096);render(a,x);render(b,y);CHECK(same(x,y));
}
void exactSpuPath() {
  // Filter-zero packets independently define their exact decoded int16 PCM.
  // A host bank must pass through the same Gaussian, envelope and stereo path.
  RawAdpcmSample raw;raw.adpcm.resize(64);raw.loopStartSamples=0;raw.volumeLeft=.75;raw.volumeRight=.25;
  std::vector<std::int16_t> decoded(112);
  for(unsigned block=0;block<4;++block){raw.adpcm[block*16]=4;raw.adpcm[block*16+1]=block==0?6:block==3?3:0;}
  for(unsigned i=0;i<112;++i){const int nibble=static_cast<int>(std::lround(7*std::sin(2*pi*i/112)));decoded[i]=static_cast<std::int16_t>(nibble*256);raw.adpcm[i/28*16+2+(i%28)/2]|=static_cast<std::uint8_t>((nibble&15)<<((i%2)*4));}
  SoftwarePcmSample software;software.pcm=SoftwarePcmBuffer::copyOf(decoded);software.baseFrequencyHz=raw.baseFrequencyHz;software.loop=true;software.volumeLeft=.75;software.volumeRight=.25;
  for(double rate:{44100.,48000.,96000.}) {
    SpuInstrument a(raw),b(software);CHECK(a.prepare(rate)&&b.prepare(rate));a.noteOn(69,127);b.noteOn(69,127);Stereo x(8193),y(8193);render(a,x,1);render(b,y,127);CHECK(same(x,y));
  }
}
void morphIsDormant() {
  const auto source=sine();SpuInstrument edited(source),reference(source);Stereo a(1031),b(1031);
  MorphEndpoints endpoints;endpoints.a.fill(.7f);endpoints.b.fill(-.2f);
  CHECK(edited.setMorphEndpoints(endpoints)&&edited.setMorph({true,.9}));
  CHECK(edited.morph().enabled&&!edited.effectiveMorphEnabled());
  edited.noteOn(60,127);reference.noteOn(60,127);render(edited,a);render(reference,b);CHECK(same(a,b));
  for(unsigned i=0;i<8;++i) {
    CHECK(edited.setMorph({bool(i%2),double(i)/7}));endpoints.a[0]=float(i)/10;CHECK(edited.setMorphEndpoints(endpoints));
    CHECK(edited.activeVoiceCount()==1&&edited.morph().enabled==bool(i%2)&&!edited.effectiveMorphEnabled()&&edited.morphEndpoints().a==endpoints.a);
    render(edited,a);render(reference,b);CHECK(same(a,b));
  }
  PmonSettings pmon;pmon.enabled=true;pmon.decayMs=0;pmon.depth=.5;
  for(auto* core:{&edited,&reference}){CHECK(core->setPmon(pmon));core->noteOn(69,127);}
  render(edited,a);render(reference,b);CHECK(same(a,b)&&peak(a.l)>.0005);
  CHECK(edited.setMorph({true,.17}));render(edited,a);render(reference,b);CHECK(same(a,b));
  for(auto* core:{&edited,&reference}){CHECK(core->setNoise({true,48}));core->noteOn(60,127);}
  render(edited,a);render(reference,b);CHECK(same(a,b));
  for(auto* core:{&edited,&reference}){CHECK(core->setNoise({false,48}));core->noteOn(60,127);}
  render(edited,a);render(reference,b);CHECK(same(a,b));
}
void longAndEnd() {
  for(bool loop:{false,true}) {
    SoftwarePcmSample source;source.pcm=SoftwarePcmBuffer::copyOf(std::vector<std::int16_t>(1048576,12000));source.loop=loop;
    SpuInstrument core(source);Stereo pcm(1024);core.noteOn(84,127);
    for(unsigned n=0;n<250;++n)render(core,pcm);CHECK(peak(pcm.l)>.002);
    for(unsigned n=0;n<12;++n)render(core,pcm);CHECK(loop?peak(pcm.l)>.002:peak(pcm.l)==0);
    core.panic();render(core,pcm);CHECK(peak(pcm.l)==0);
    // UINT32 scheduler boundary at a low PITCH must neither stop immediately
    // nor compute unsigned negative loop overshoot (SDK regression).
    source.baseFrequencyHz=1000000;SpuInstrument low(source);low.noteOn(0,127);Stereo slow(16384);render(low,slow);CHECK(peak(slow.l)>.0005);
  }
  auto tiny=sine(4);tiny.loop=true;SpuInstrument loop(tiny);loop.noteOn(127,127);Stereo pcm(4096);render(loop,pcm);CHECK(peak(pcm.l)>0);
  auto one=sine(1);one.loop=false;SpuInstrument noise(one);CHECK(noise.setNoise({true,48}));noise.noteOn(60,127);render(noise,pcm);CHECK(peak(pcm.l)>.0005);
  SoftwarePcmSample maximum;maximum.pcm=SoftwarePcmBuffer::copyOf(std::vector<std::int16_t>(kMaxSoftwarePcmFrames,12000));
  SpuInstrument maxCore(maximum);maxCore.noteOn(60,127);render(maxCore,pcm);CHECK(peak(pcm.l)>.002);
}
void callbackAndPartitions() {
  auto source=sine(1009);source.loop=true;source.envelope.adsr2=0x3fdf;source.volumeLeft=-.75;source.reverb=true;
  MorphEndpoints endpoints;endpoints.a.fill(.7f);endpoints.b.fill(-.2f);
  for(double rate:{8000.,44100.,48000.,96000.}) {
    SpuInstrument a(source),b(source);Stereo whole(1031),parts(1031);bool ok=true;allocations=frees=0;tracking=true;
    ok=a.prepare(rate)&&b.prepare(rate);
    for(unsigned stage=0;stage<24;++stage) {
      for(auto* core:{&a,&b}) {
        ok=core->setMorph({bool(stage%2),(stage%11)/10.})&&core->setMorphEndpoints(endpoints)&&ok;
        ok=core->setNoise({stage/8==1,stage%64})&&ok;
        PmonSettings p;p.enabled=stage/8==2;p.waveform=static_cast<WaveformShape>(stage%4);p.ratio=.5+(stage%4);p.depth=(stage%11)/10.;
        ok=core->setPmon(p)&&ok;
        for(int note=0;note<24;++note)core->noteOn(36+note,stage%3?127:1,note%16);
        core->setPitchBend((stage*701)%16384,stage%16);core->setSustain(stage%2,stage%16);core->noteOff(36+stage,stage%16);
        ReverbSettings reverb;reverb.enabled=stage%4==1;reverb.preset=ReverbPreset::Room;ok=core->setReverb(reverb)&&ok;
        if(stage%7==0){core->panic();core->noteOn(60,64);}
      }
      render(a,whole,1031);render(b,parts,7);CHECK(same(whole,parts));
      CHECK(a.activeVoiceCount()==b.activeVoiceCount()&&a.activeVoiceCount()<=(stage/8==2?12:24));
    }
    a.panic();b.panic();render(a,whole);render(b,parts);tracking=false;
    CHECK(ok&&allocations==0&&frees==0&&peak(whole.l)==0);
  }
  auto source2=sine();SpuInstrument all(source2);for(int note=0;note<24;++note)all.noteOn(36+note,127);CHECK(all.activeVoiceCount()==24);
  PmonSettings p;p.enabled=true;p.depth=0;CHECK(all.setPmon(p));for(int note=0;note<24;++note)all.noteOn(36+note,127);CHECK(all.activeVoiceCount()==12);
}
void validation() {
  auto good=sine();for(unsigned i=0;i<5;++i){auto bad=good;if(i==0)bad.pcm.reset();if(i==1)bad.baseFrequencyHz=0;if(i==2)bad.volumeLeft=1.01;if(i==3)bad.volumeRight=std::numeric_limits<double>::quiet_NaN();if(i==4)bad.inspectorPitch=16384;rejects([&]{SpuInstrument synth(bad);});}
  auto bad=sine(3);rejects([&]{SpuInstrument synth(bad);});
  SpuInstrument normal;CHECK(!normal.effectiveMorphEnabled());CHECK(normal.setMorph({true,.5})&&normal.effectiveMorphEnabled());
  CHECK(normal.setNoise({true,48})&&!normal.effectiveMorphEnabled()&&normal.morph().enabled);
  CHECK(normal.setNoise({false,48})&&normal.effectiveMorphEnabled());
}
}
int main(){playbackAndPitch();exactSpuPath();morphIsDormant();longAndEnd();callbackAndPartitions();validation();std::puts("PASS Software PCM full32MiB/long END+loop/low-PITCH; Gaussian exact RAM oracle; pitch/bend/sustain/source ADSR/send/signed velocity; dormant Morph; Noise/PMON24/12; independent rates/partitions; first callback new/delete0");}
