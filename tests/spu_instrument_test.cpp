// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "VabBank.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

namespace {
thread_local bool gTrackAllocations = false;
thread_local std::size_t gAllocations = 0, gFrees = 0;
void* allocate(std::size_t size) {
  if (gTrackAllocations) ++gAllocations;
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void release(void* value) noexcept {
  if (gTrackAllocations && value) ++gFrees;
  std::free(value);
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { release(value); }
void operator delete[](void* value) noexcept { release(value); }
void operator delete(void* value, std::size_t) noexcept { release(value); }
void operator delete[](void* value, std::size_t) noexcept { release(value); }

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while (0)
using tetorica::SpuInstrument;
using tetorica::EnvelopeRegisters;
using tetorica::ReverbPreset;
using tetorica::ReverbSettings;
namespace {
struct Stereo {
  std::vector<float> left, right;
  explicit Stereo(int frames) : left(frames), right(frames) {}
};
void render(SpuInstrument& synth, Stereo& pcm, int block = 257) {
  for (int at=0; at<static_cast<int>(pcm.left.size());) {
    const int n=std::min(block, static_cast<int>(pcm.left.size())-at);
    synth.render(pcm.left.data()+at, pcm.right.data()+at, n);
    at+=n;
  }
  CHECK(synth.healthy());
  for (std::size_t i=0; i<pcm.left.size(); ++i) {
    CHECK(std::isfinite(pcm.left[i]) && std::isfinite(pcm.right[i]));
    CHECK(std::abs(pcm.left[i]) <= 1 && std::abs(pcm.right[i]) <= 1);
  }
}
float peak(const Stereo& pcm, int start=0) {
  float value=0;
  for (int i=start; i<static_cast<int>(pcm.left.size()); ++i) value=std::max(value,std::abs(pcm.left[i]));
  return value;
}
double frequency(const Stereo& pcm, double rate) {
  int first=-1,last=-1,crossings=0;
  for (int i=4097; i<static_cast<int>(pcm.left.size()); ++i)
    if (pcm.left[i-1] <= 0 && pcm.left[i] > 0) {
      if (first < 0) first=i;
      last=i; ++crossings;
    }
  CHECK(crossings>10);
  return (crossings-1)*rate/(last-first);
}
double expectedFrequency(int note,double semitones=0){
  const double root=44100./112;
  const auto pitch=std::lround(4096*440*std::exp2((note-69+semitones)/12)/root);
  return std::clamp(pitch,1l,16383l)*root/4096;
}
double magnitude(const Stereo& pcm,double hz,double rate){
  double real=0,imaginary=0;const auto count=pcm.left.size()-4096;
  for(std::size_t i=4096;i<pcm.left.size();++i){
    const double phase=2*3.14159265358979323846*hz*i/rate;
    real+=pcm.left[i]*std::cos(phase);imaginary+=pcm.left[i]*std::sin(phase);
  }
  return std::sqrt(real*real+imaginary*imaginary)/count;
}
double energy(const Stereo& pcm){
  double total=0;for(float value:pcm.left)total+=double(value)*value;
  return total;
}
tetorica::vab::PreparedTone longReleaseTone(){
  tetorica::vab::PreparedTone tone;
  tone.adpcm={
    38,6,68,85,85,102,117,118,102,119,119,118,103,103,87,102,
    38,0,70,70,69,52,36,35,18,17,0,224,224,222,222,220,
    38,0,188,188,171,171,170,154,154,169,153,169,153,170,169,155,
    38,3,172,187,204,204,237,221,255,14,240,2,33,34,51,67};
  tone.rootFrequencyHz=44100./112;tone.decodedFrames=112;tone.loopStartSamples=0;
  tone.adsr1=15;tone.adsr2=0x1fd0; //Fast attack, held sustain, ~3-second linear release.
  tone.volumeLeft=tone.volumeRight=1;
  return tone;
}
void testRatesPitchAndRelease() {
  for (double rate : {8000., 44100., 48000., 88200., 96000., 192000., 384000.}) {
    SpuInstrument synth;
    CHECK(synth.prepare(rate));
    Stereo idle(600),tone(static_cast<int>(rate)),tail(2048);
    render(synth,idle); CHECK(peak(idle)==0);
    synth.noteOn(69,127); CHECK(synth.activeVoiceCount()==1);
    render(synth,tone);
    CHECK(peak(tone)>.02f && peak(tone)<.05f);
    CHECK(std::abs(frequency(tone,rate)-440)<.3);
    CHECK(tone.left==tone.right);
    synth.noteOff(69); CHECK(synth.activeVoiceCount()==0);
    render(synth,tail);
    // Release is at least128 native samples; high host rates need more host
    // frames, so use a second tail rather than asserting a host-frame constant.
    render(synth,tail); CHECK(peak(tail)==0);
  }
}
void testPartitionInvariance() {
  for (double rate : {8000.,44100.,48000.,96000.,384000.}) {
    SpuInstrument a,b;
    CHECK(a.prepare(rate) && b.prepare(rate));
    Stereo whole(48013),parts(48013);
    a.noteOn(60,100); b.noteOn(60,100);
    render(a,whole,48013); render(b,parts,7);
    CHECK(whole.left==parts.left && whole.right==parts.right);
    CHECK(a.nativeFramesRendered()==b.nativeFramesRendered());
    a.noteOn(64,110); b.noteOn(64,110);
    a.noteOff(60); b.noteOff(60);
    render(a,whole,48013); render(b,parts,127);
    CHECK(whole.left==parts.left && whole.right==parts.right);
    a.allNotesOff(); b.allNotesOff();
    render(a,whole,1024); render(b,parts,1);
    CHECK(whole.left==parts.left && whole.right==parts.right);
  }
}
void testPolyphonyChannelsAndPanic() {
  SpuInstrument synth;
  CHECK(synth.prepare(48000));
  Stereo pcm(8192);
  for (int note=40; note<64; ++note) synth.noteOn(note,127);
  CHECK(synth.activeVoiceCount()==24);
  render(synth,pcm); CHECK(peak(pcm)>.02f);
  synth.noteOn(70,127); CHECK(synth.activeVoiceCount()==24); // Oldest40 stolen.
  synth.noteOff(40); CHECK(synth.activeVoiceCount()==24);    // Late Off cannot stop70.
  synth.noteOff(70); CHECK(synth.activeVoiceCount()==23);
  synth.panic(); CHECK(synth.activeVoiceCount()==0);
  render(synth,pcm); CHECK(peak(pcm)==0);
  for (int ch=0; ch<16; ++ch) synth.noteOn(60,100,ch);
  CHECK(synth.activeVoiceCount()==16);
  synth.noteOn(60,110,0); CHECK(synth.activeVoiceCount()==16); // Same identity retriggers.
  synth.allNotesOff(0); CHECK(synth.activeVoiceCount()==15);
  synth.noteOn(60,0,1); CHECK(synth.activeVoiceCount()==14);  // Velocity0 means Off.
  synth.allNotesOff(); CHECK(synth.activeVoiceCount()==0);
  render(synth,pcm); CHECK(peak(pcm,2048)==0);
  // Worst-case aligned voices must not saturate the SPU mixer internally.
  synth.panic();
  for (int ch=0; ch<16; ++ch) synth.noteOn(69,127,ch);
  for (int ch=0; ch<8; ++ch) synth.noteOn(68,127,ch);
  render(synth,pcm); CHECK(peak(pcm)>.4f && peak(pcm)<.95f);
}
void testInvalidRatesAndInstanceIsolation() {
  SpuInstrument a,b;
  CHECK(!a.prepare(std::numeric_limits<double>::quiet_NaN()));
  CHECK(!a.prepare(std::numeric_limits<double>::infinity()));
  CHECK(!a.prepare(7999) && !a.prepare(384001));
  CHECK(a.sampleRate()==44100);
  a.noteOn(-1,127); a.noteOn(128,127); a.noteOn(60,-1);
  a.noteOn(60,128); a.noteOn(60,127,16); a.noteOn(60,127,-1);
  CHECK(a.activeVoiceCount()==0);
  a.noteOn(60,100); b.noteOn(60,100);
  Stereo pa(4097),pb(4097);
  render(a,pa); render(b,pb); CHECK(pa.left==pb.left);
  a.panic(); render(a,pa); render(b,pb);
  CHECK(peak(pa)==0 && peak(pb)>.01f);
  CHECK(b.prepare(96000)); CHECK(b.activeVoiceCount()==0);
  render(b,pb); CHECK(peak(pb)==0);
  b.noteOn(72,127); render(b,pb); CHECK(peak(pb)>.01f);
}
void testMidiOffset() {
  SpuInstrument synth;
  CHECK(synth.prepare(48000));
  Stereo pcm(4096);
  synth.render(pcm.left.data(),pcm.right.data(),193);
  synth.noteOn(69,127);
  synth.render(pcm.left.data()+193,pcm.right.data()+193,4096-193);
  CHECK(std::all_of(pcm.left.begin(),pcm.left.begin()+193,[](float value){return value==0;}));
  CHECK(peak(pcm,193)>.02f);
}
void testPitchBend(){
  //Endpoint PCM must equal independent semitone transposition, including new
  //notes on a second channel which must remain unaffected by the first bend.
  for(const auto pair:{std::array<int,2>{0,67},{4096,68},{8192,69},{16383,71}}){
    SpuInstrument a,b;CHECK(a.prepare(48000)&&b.prepare(48000));
    a.setPitchBend(pair[0],3);a.noteOn(69,127,3);a.noteOn(60,100,4);
    b.noteOn(pair[1],127,3);b.noteOn(60,100,4);
    Stereo out(48000),reference(48000);render(a,out);render(b,reference,7);
    CHECK(out.left==reference.left&&out.right==reference.right);
  }
  SpuInstrument held;CHECK(held.prepare(44100));
  held.noteOn(69,127,0);held.noteOn(60,127,1);
  Stereo warm(4099),out(44100);render(held,warm);
  held.setPitchBend(16383,0);render(held,out);
  CHECK(magnitude(out,expectedFrequency(69,2),44100)>.015);
  CHECK(magnitude(out,expectedFrequency(60),44100)>.015);
  CHECK(magnitude(out,expectedFrequency(60,2),44100)<.002);

  //Bend a released but still audible voice. Its first sample remains identical
  //to an unbent continuation: restarting its ADPCM/envelope would fail here.
  auto tone=longReleaseTone();SpuInstrument a(tone),b(tone);
  CHECK(a.prepare(44100)&&b.prepare(44100));a.noteOn(69,127,15);b.noteOn(69,127,15);
  Stereo before(4099),copy(4099);render(a,before);render(b,copy);CHECK(before.left==copy.left);
  a.noteOff(69,15);b.noteOff(69,15);a.setPitchBend(0,15);
  std::array<float,1> al{},ar{},bl{},br{};
  a.render(al.data(),ar.data(),1);b.render(bl.data(),br.data(),1);
  CHECK(al[0]==bl[0]&&std::abs(al[0])>.005f&&a.activeVoiceCount()==0);
  render(a,out);CHECK(std::abs(frequency(out,44100)-expectedFrequency(69,-2))<.3);
  CHECK(peak(out)>.02f);
}
void testSustainAndAllNotesOff(){
  SpuInstrument a,b;CHECK(a.prepare(48000)&&b.prepare(48000));
  a.setSustain(true,0);a.noteOn(69,127,0);a.noteOn(60,100,1);
  b.noteOn(69,127,0);b.noteOn(60,100,1);
  Stereo out(8192),reference(8192);render(a,out);render(b,reference);
  a.allNotesOff();b.noteOff(60,1); //CC123 respects pedal0, but channel1 releases.
  CHECK(a.activeVoiceCount()==0&&b.activeVoiceCount()==1);
  render(a,out);render(b,reference);CHECK(out.left==reference.left&&peak(out)>.02f);
  a.setSustain(false,0);b.noteOff(69,0);
  render(a,out);render(b,reference);CHECK(out.left==reference.left&&peak(out,2048)==0);

  a.setSustain(true,3);a.noteOn(69,127,3);b.noteOn(69,127,3);
  render(a,out);render(b,reference);CHECK(out.left==reference.left);
  a.noteOff(69,3); //Leave hardware gate unchanged while the pedal is held.
  render(a,out);render(b,reference);CHECK(out.left==reference.left);
  a.noteOn(69,127,3);b.noteOn(69,127,3); //Deferred retrigger reuses the same slot.
  render(a,out);render(b,reference);CHECK(out.left==reference.left&&a.activeVoiceCount()==1);
  a.setSustain(false,3); //Must not release a physically held retriggered key.
  render(a,out);render(b,reference);CHECK(out.left==reference.left&&peak(out)>.02f);
  a.noteOff(69,3);b.noteOff(69,3);render(a,out);render(b,reference);
  CHECK(out.left==reference.left&&peak(out,2048)==0);
}
void testDeferredVoiceAllocation(){
  SpuInstrument a,b;CHECK(a.prepare(44100)&&b.prepare(44100));
  a.setSustain(true,0);a.noteOn(69,127,0);b.noteOn(69,127,0);
  Stereo out(8192),reference(8192);render(a,out);render(b,reference);
  a.noteOff(69,0);
  for(int note=40;note<63;++note){a.noteOn(note,100,1);b.noteOn(note,100,1);}
  CHECK(a.activeVoiceCount()==23&&b.activeVoiceCount()==24);
  render(a,out);render(b,reference);CHECK(out.left==reference.left);
  //All24 slots are busy (one deferred); oldest deferred voice is stolen.
  a.noteOn(75,100,2);b.noteOn(75,100,2);
  CHECK(a.activeVoiceCount()==24&&b.activeVoiceCount()==24);
  a.setSustain(false,0);a.noteOff(69,0);b.noteOff(69,0);
  render(a,out);render(b,reference);CHECK(out.left==reference.left);
}
void testControllerResetPanicAndInvalid(){
  SpuInstrument synth;CHECK(synth.prepare(44100));
  synth.setPitchBend(16383,0);synth.setPitchBend(0,1);synth.setSustain(true,0);
  synth.noteOn(69,127,0);synth.noteOn(60,127,1);synth.noteOn(67,127,0);
  Stereo warm(4099),out(44100);render(synth,warm);synth.noteOff(67,0);
  synth.resetControllers(0);CHECK(synth.activeVoiceCount()==2);render(synth,out);
  CHECK(magnitude(out,expectedFrequency(69),44100)>.015);
  CHECK(magnitude(out,expectedFrequency(60,-2),44100)>.015);
  CHECK(magnitude(out,expectedFrequency(67),44100)<.002);
  synth.resetControllers();render(synth,out);
  CHECK(magnitude(out,expectedFrequency(60),44100)>.015&&synth.activeVoiceCount()==2);
  synth.allNotesOff();render(synth,out);CHECK(peak(out,2048)==0);

  synth.setPitchBend(16383,15);synth.setSustain(true,15);synth.noteOn(69,127,15);
  synth.panic();synth.noteOn(69,127,15);render(synth,warm);synth.noteOff(69,15);
  render(synth,out);CHECK(peak(out)>.02f&&synth.activeVoiceCount()==0);
  CHECK(std::abs(frequency(out,44100)-expectedFrequency(69,2))<.3); //Panic retains both controllers.
  CHECK(!synth.prepare(0));render(synth,out);CHECK(peak(out)>.02f);
  CHECK(synth.prepare(44100));synth.noteOn(69,127,15);render(synth,out);
  CHECK(std::abs(frequency(out,44100)-expectedFrequency(69))<.3);
  synth.noteOff(69,15);render(synth,out);CHECK(peak(out,2048)==0); //Valid prepare resets pedal too.

  SpuInstrument a,b;CHECK(a.prepare(48000)&&b.prepare(48000));
  a.noteOn(69,127,0);a.noteOn(60,100,15);b.noteOn(69,127,0);b.noteOn(60,100,15);
  a.setPitchBend(-1,0);a.setPitchBend(16384,0);a.setPitchBend(0,-1);a.setPitchBend(16383,16);
  a.setSustain(true,-1);a.setSustain(true,16);a.resetControllers(-2);a.resetControllers(16);
  Stereo compared(out.left.size());render(a,out);render(b,compared);CHECK(out.left==compared.left);
  a.allNotesOff();b.allNotesOff();render(a,out);render(b,compared);
  CHECK(out.left==compared.left&&peak(out,2048)==0);
}
void testControllerPartitionInvariance(){
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a,b;CHECK(a.prepare(rate)&&b.prepare(rate));
    Stereo whole(4099),parts(4099);
    a.noteOn(69,127,4);b.noteOn(69,127,4);
    for(int stage=0;stage<8;++stage){
      switch(stage){
        case 0:a.setSustain(true,4);b.setSustain(true,4);break;
        case 1:a.noteOff(69,4);b.noteOff(69,4);break;
        case 2:a.setPitchBend(16383,4);b.setPitchBend(16383,4);break;
        case 3:a.noteOn(69,127,4);b.noteOn(69,127,4);break;
        case 4:a.allNotesOff(4);b.allNotesOff(4);break;
        case 5:a.setPitchBend(0,4);b.setPitchBend(0,4);break;
        case 6:a.resetControllers(4);b.resetControllers(4);break;
        case 7:a.panic();b.panic();break;
      }
      render(a,whole,4099);render(b,parts,7);
      CHECK(whole.left==parts.left&&whole.right==parts.right);
    }
  }
}
void testEnvelopeDefaultsAndOwnership(){
  SpuInstrument builtin;
  CHECK(builtin.sourceEnvelope().adsr1==0x000f&&builtin.sourceEnvelope().adsr2==0x1fc0);
  CHECK(builtin.envelope().adsr1==0x000f&&builtin.envelope().adsr2==0x1fc0);
  const EnvelopeRegisters custom{0x9234,0xa56b};builtin.setEnvelope(custom,false);
  CHECK(builtin.envelope().adsr1==custom.adsr1&&builtin.envelope().adsr2==custom.adsr2);
  CHECK(builtin.prepare(48000));builtin.panic();builtin.resetControllers();
  CHECK(builtin.envelope().adsr1==custom.adsr1&&builtin.envelope().adsr2==custom.adsr2);
  CHECK(builtin.sourceEnvelope().adsr1==0x000f&&builtin.sourceEnvelope().adsr2==0x1fc0);

  auto tone=longReleaseTone();tone.adsr1=0x4183;tone.adsr2=0xa56b;
  SpuInstrument imported(tone);tone.adsr1=tone.adsr2=0; //Source is an owned raw value.
  CHECK(imported.sourceEnvelope().adsr1==0x4183&&imported.sourceEnvelope().adsr2==0xa56b);
  imported.setEnvelope({0x000f,0x1fc0});
  CHECK(imported.envelope().adsr2==0x1fc0); //All16 raw bits, including bit13, follow the caller.
  CHECK(imported.sourceEnvelope().adsr2==0xa56b);
}
void testLiveEnvelopeStages(){
  for(int stage=0;stage<4;++stage){
    auto tone=longReleaseTone();
    tone.adsr1=stage==0?0x400f:stage==1?0x00c0:0x000f;
    tone.adsr2=0x1fce;
    SpuInstrument live(tone),deferred(tone);
    CHECK(live.prepare(44100)&&deferred.prepare(44100));
    live.noteOn(69,127);deferred.noteOn(69,127);
    Stereo before(stage==0?101:stage==1?300:1009),copy(static_cast<int>(before.left.size()));
    render(live,before);render(deferred,copy);CHECK(before.left==copy.left);
    if(stage==3){
      live.noteOff(69);deferred.noteOff(69);Stereo tail(53);
      render(live,tail);render(deferred,tail);
    }
    const auto frame=live.nativeFramesRendered();
    const EnvelopeRegisters patch=stage==0?EnvelopeRegisters{0x100f,0x1fce}:
      stage==1?EnvelopeRegisters{0x0040,0x1fce}:
      stage==2?EnvelopeRegisters{0x000f,0x4a0e}:EnvelopeRegisters{0x000f,0x1fc0};
    live.setEnvelope(patch,true);deferred.setEnvelope(patch,false);
    CHECK(live.nativeFramesRendered()==frame);
    CHECK(live.activeVoiceCount()==(stage==3?0:1));
    Stereo changed(4096),unchanged(4096);render(live,changed);render(deferred,unchanged);
    CHECK(changed.left.front()==unchanged.left.front()&&changed.right.front()==unchanged.right.front());
    if(stage==0)CHECK(energy(changed)>energy(unchanged)*4);
    else CHECK(energy(changed)<energy(unchanged)*.5);
    if(stage==2||stage==3)CHECK(peak(changed,3000)==0&&peak(unchanged,3000)>.02f);
  }
}
void testDeferredEnvelopeAndNextNote(){
  auto tone=longReleaseTone();tone.adsr2=0x1fce;
  SpuInstrument edited(tone),reference(tone),unmodified(tone);
  for(auto* synth:{&edited,&reference,&unmodified}){CHECK(synth->prepare(44100));synth->noteOn(69,127);}
  Stereo a(4096),b(4096),c(4096);render(edited,a);render(reference,b);render(unmodified,c);
  const EnvelopeRegisters slowAttackFastRelease{0x640f,0x1fc0};
  edited.setEnvelope(slowAttackFastRelease,false);render(edited,a);render(reference,b);
  CHECK(a.left==b.left&&a.right==b.right); //Old held envelope is bit-identical.
  edited.noteOff(69);reference.noteOff(69);render(edited,a);render(reference,b);
  CHECK(a.left==b.left&&a.right==b.right&&peak(a,3000)>.02f); //KOFF retains old RR14.
  for(auto* synth:{&edited,&reference,&unmodified})synth->panic();
  reference.setEnvelope(slowAttackFastRelease,false);
  for(auto* synth:{&edited,&reference,&unmodified})synth->noteOn(69,127);
  render(edited,a);render(reference,b);render(unmodified,c);
  CHECK(a.left==b.left&&a.right==b.right);
  //Ignore the128-native-frame audition crossfade from the preceding tail.
  CHECK(peak(a,512)<peak(c,512)*.01f);
  CHECK(edited.sourceEnvelope().adsr1==tone.adsr1&&edited.sourceEnvelope().adsr2==tone.adsr2);
}
void testEnvelopePhaseAndExplicitApply(){
  SpuInstrument live,deferred;CHECK(live.prepare(44100)&&deferred.prepare(44100));
  live.noteOn(69,127);deferred.noteOn(69,127);
  Stereo a(4099),b(4099);render(live,a);render(deferred,b);
  live.setEnvelope({0x7f0f,0x1fc0},true);deferred.setEnvelope({0x7f0f,0x1fc0},false);
  render(live,a);render(deferred,b);CHECK(a.left==b.left&&a.right==b.right);
  //AR changes during sustain preserve every sample/phase, not just the first.
  live.setEnvelope({0x7f03,0x1fc0},true);deferred.setEnvelope({0x7f03,0x1fc0},false);
  render(live,a);render(deferred,b);CHECK(a.left==b.left&&a.right==b.right);
  //SL is a future decay target, so editing it during sustain never jumps level.
  const EnvelopeRegisters falling{0x7f03,0x4a00};
  live.setEnvelope(falling,false);deferred.setEnvelope(falling,false);
  render(live,a);render(deferred,b);CHECK(a.left==b.left&&a.right==b.right);
  live.setEnvelope(live.envelope(),true); //Apply off->on must flush unchanged raw words.
  render(live,a);render(deferred,b);
  CHECK(a.left.front()==b.left.front()&&energy(a)<energy(b)*.5&&peak(a,3000)==0);
}
void testEnvelopePolyphonyPedalAndRelease(){
  auto tone=longReleaseTone();tone.adsr2=0x1fce;
  SpuInstrument live(tone),deferred(tone);
  for(auto* synth:{&live,&deferred}){
    CHECK(synth->prepare(44100));synth->setSustain(true,0);
    for(int i=0;i<24;++i)synth->noteOn(60+i%2,100,i/2);
  }
  Stereo a(1009),b(1009);render(live,a);render(deferred,b);CHECK(a.left==b.left);
  for(auto* synth:{&live,&deferred}){synth->noteOff(60,0);synth->noteOff(60,1);}
  CHECK(live.activeVoiceCount()==22); //One pedal-held voice and one release tail.
  const EnvelopeRegisters falling{0x000f,0x4a00};
  live.setEnvelope(falling,true);deferred.setEnvelope(falling,false);
  Stereo changed(4096),unchanged(4096);render(live,changed);render(deferred,unchanged);
  CHECK(changed.left.front()==unchanged.left.front());
  CHECK(peak(changed,3000)==0&&peak(unchanged,3000)>.02f&&live.activeVoiceCount()==22);
  live.setSustain(false,0); //Releasing the now-silent deferred key cannot restart it.
  render(live,changed);CHECK(peak(changed)==0);
}
void testEnvelopePartitionInvariance(){
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a,b;CHECK(a.prepare(rate)&&b.prepare(rate));
    a.setEnvelope({0x400f,0x1fce},false);b.setEnvelope({0x400f,0x1fce},false);
    a.noteOn(69,127);b.noteOn(69,127);
    Stereo whole(173),parts(173);
    for(unsigned stage=0;stage<64;++stage){
      const EnvelopeRegisters values{static_cast<std::uint16_t>((stage*65521)^0x1327),
        static_cast<std::uint16_t>((stage*27847)^0xa875)};
      if(stage==37){a.noteOff(69);b.noteOff(69);}
      a.setEnvelope(values,stage%3!=0);b.setEnvelope(values,stage%3!=0);
      render(a,whole,173);render(b,parts,7);
      CHECK(whole.left==parts.left&&whole.right==parts.right);
    }
  }
}
bool sameReverb(ReverbSettings a,ReverbSettings b){
  return a.preset==b.preset&&a.enabled==b.enabled&&a.useToneSend==b.useToneSend&&
    a.volumeLeft==b.volumeLeft&&a.volumeRight==b.volumeRight;
}
void testReverbDefaultsAndValidation(){
  auto tone=longReleaseTone();tone.adsr2=0x1fc0;tone.reverb=true;
  SpuInstrument imported(tone);tone.reverb=false;SpuInstrument dry(tone),builtin;
  CHECK(imported.sourceReverbSend()&&!dry.sourceReverbSend()&&!builtin.sourceReverbSend());
  CHECK(sameReverb(imported.reverb(),{})&&imported.setReverb({}));
  CHECK(imported.prepare(44100)&&dry.prepare(44100));
  imported.noteOn(69,127);dry.noteOn(69,127);Stereo a(8192),b(8192);
  render(imported,a);render(dry,b);CHECK(a.left==b.left&&a.right==b.right);
  const ReverbSettings disabled{ReverbPreset::Room,false,false,.3,.4};
  CHECK(imported.setReverb(disabled));render(imported,a);render(dry,b);
  CHECK(a.left==b.left&&a.right==b.right); //A selected but disabled preset is exact dry.
  for(int kind=0;kind<7;++kind){
    auto invalid=disabled;
    switch(kind){
      case 0:invalid.preset=static_cast<ReverbPreset>(8);break;
      case 1:invalid.preset=static_cast<ReverbPreset>(0xffffffffu);break;
      case 2:invalid.volumeLeft=-.01;break;
      case 3:invalid.volumeRight=1.01;break;
      case 4:invalid.volumeLeft=std::numeric_limits<double>::quiet_NaN();break;
      case 5:invalid.volumeRight=std::numeric_limits<double>::infinity();break;
      case 6:invalid.volumeLeft=-std::numeric_limits<double>::infinity();break;
    }
    CHECK(!imported.setReverb(invalid)&&sameReverb(imported.reverb(),disabled)&&imported.healthy());
    render(imported,a);render(dry,b);CHECK(a.left==b.left&&a.right==b.right);
  }
  CHECK(imported.sourceReverbSend());
}
void testReverbPresetsAndStereoReturns(){
  std::vector<float> previous;
  for(unsigned preset=0;preset<8;++preset){
    SpuInstrument wet,dry;CHECK(wet.prepare(44100)&&dry.prepare(44100));
    wet.noteOn(69,127);dry.noteOn(69,127);
    Stereo before(1009),copy(1009);render(wet,before);render(dry,copy);
    ReverbSettings settings{static_cast<ReverbPreset>(preset),true,false,0,.75};
    const auto frames=wet.nativeFramesRendered();CHECK(wet.setReverb(settings));
    CHECK(wet.nativeFramesRendered()==frames&&wet.activeVoiceCount()==1);
    Stereo a(8192),b(8192);render(wet,a);render(dry,b);
    CHECK(a.left==b.left); //Muted return preserves every dry sample/phase without KON.
    if(preset){CHECK(a.right!=b.right);if(!previous.empty())CHECK(a.right!=previous);previous=a.right;}
    else CHECK(a.right==b.right);
    wet.noteOff(69);dry.noteOff(69);Stereo tail(16000);render(wet,tail);
    CHECK(peak(tail,2048)==0);
    float right=0;for(std::size_t i=2048;i<tail.right.size();++i)right=std::max(right,std::abs(tail.right[i]));
    CHECK(preset?right>.00001f:right==0);
    if(preset){
      settings.volumeLeft=.75;settings.volumeRight=0;CHECK(wet.setReverb(settings));
      Stereo flipped(4096);render(wet,flipped);
      CHECK(peak(flipped)>.00001f);
      CHECK(std::all_of(flipped.right.begin(),flipped.right.end(),[](float v){return v==0;}));
    }
  }
}
void testReverbBypassDecayAndToneSend(){
  SpuInstrument a,b;for(auto* synth:{&a,&b}){
    CHECK(synth->prepare(44100));CHECK(synth->setReverb({ReverbPreset::Room,true,false,.75,.75}));
    synth->noteOn(69,127);
  }
  Stereo out(8192),reference(8192);render(a,out);render(b,reference);
  a.noteOff(69);b.noteOff(69);Stereo release(1024);render(a,release);render(b,release);
  auto disabled=a.reverb();disabled.enabled=false;CHECK(a.setReverb(disabled));
  render(a,out);render(b,reference);CHECK(peak(out)==0&&peak(reference)>.00001f);
  disabled.enabled=true;CHECK(a.setReverb(disabled));render(a,out);render(b,reference);
  CHECK(out.left==reference.left&&out.right==reference.right); //Hidden tail kept decaying, not frozen/cleared.

  auto tone=longReleaseTone();tone.adsr2=0x1fc0;
  SpuInstrument filtered(tone),dry(tone);tone.reverb=true;SpuInstrument sourceSend(tone);
  const ReverbSettings toneSend{ReverbPreset::Room,true,true,.75,.75};
  for(auto* synth:{&filtered,&dry,&sourceSend}){CHECK(synth->prepare(44100));synth->noteOn(69,127);}
  CHECK(filtered.setReverb(toneSend)&&sourceSend.setReverb(toneSend));
  render(filtered,out);render(dry,reference);CHECK(out.left==reference.left);
  render(sourceSend,out);CHECK(out.left!=reference.left); //Mode bit4 selects imported Tone sends.
  auto all=toneSend;all.useToneSend=false;CHECK(filtered.setReverb(all));
  render(filtered,out);render(dry,reference);CHECK(out.left.front()==reference.left.front()&&out.left!=reference.left);
  CHECK(!filtered.sourceReverbSend()&&sourceSend.sourceReverbSend());
}
void testReverbOffPanicAndPrepare(){
  for(double rate:{44100.,48000.}){
    SpuInstrument wet,dry,fresh;for(auto* synth:{&wet,&dry,&fresh})CHECK(synth->prepare(rate));
    const ReverbSettings effect{ReverbPreset::SpaceEcho,true,false,.75,.75};
    CHECK(wet.setReverb(effect));wet.noteOn(69,127);dry.noteOn(69,127);
    Stereo before(16384),reference(16384);render(wet,before);render(dry,reference);
    auto off=effect;off.preset=ReverbPreset::Off;CHECK(wet.setReverb(off));
    Stereo settle(16);render(wet,settle);render(dry,settle);
    Stereo out(257),copy(257);render(wet,out);render(dry,copy);
    CHECK(out.left==copy.left&&out.right==copy.right&&wet.activeVoiceCount()==1);
    CHECK(wet.setReverb(effect)&&dry.setReverb(effect));render(wet,out);render(dry,copy);
    CHECK(out.left==copy.left&&out.right==copy.right); //Equals a fresh effect at the same sample phase.
    render(wet,before);wet.panic();wet.panic();render(wet,before);CHECK(peak(before)==0);
    CHECK(sameReverb(wet.reverb(),effect));CHECK(fresh.setReverb(effect));
    render(fresh,reference);CHECK(peak(reference)==0); //Align continuous host-rate adapter time.
    wet.noteOn(69,127);fresh.noteOn(69,127);render(wet,before);render(fresh,reference);
    CHECK(before.left==reference.left&&before.right==reference.right);
    wet.panic();wet.panic();render(wet,before);CHECK(peak(before)==0); //Rendering after clear dirties history again.
    wet.noteOn(69,127);render(wet,before);CHECK(wet.prepare(rate));render(wet,before);
    CHECK(peak(before)==0&&sameReverb(wet.reverb(),effect));
  }
}
void testOneShotReverbSendRestored(){
  auto tone=longReleaseTone();tone.adsr2=0x1fc0;tone.reverb=true;
  tone.adpcm[1]=0;tone.adpcm[49]=1;tone.loopStartSamples.reset();
  SpuInstrument a(tone),b(tone);CHECK(a.prepare(44100)&&b.prepare(44100));
  const ReverbSettings effect{ReverbPreset::Room,true,true,.75,.75};
  CHECK(a.setReverb(effect)&&b.setReverb(effect));
  Stereo out(4096),reference(4096);
  for(int slot=0;slot<24;++slot){
    a.noteOn(40+slot,100,slot%16);b.noteOn(40+slot,100,slot%16);
    render(a,out);render(b,reference);CHECK(out.left==reference.left&&peak(out)>.00001f);
    auto disabled=effect;disabled.enabled=false;CHECK(b.setReverb(disabled)&&b.setReverb(effect));
    //Only b receives an explicit send-mask restore. a must restore the bit at
    //Note On after Native one-shot END cleared it, including slots15/16/23.
    a.noteOn(40+slot,100,slot%16);b.noteOn(40+slot,100,slot%16);
    render(a,out);render(b,reference);CHECK(out.left==reference.left&&out.right==reference.right);
  }
}
void testReverbPartitionInvariance(){
  for(double rate:{8000.,44100.,48000.,96000.}){
    SpuInstrument a,b;CHECK(a.prepare(rate)&&b.prepare(rate));a.noteOn(69,127);b.noteOn(69,127);
    Stereo whole(4099),parts(4099);
    for(unsigned stage=0;stage<24;++stage){
      const ReverbSettings settings{static_cast<ReverbPreset>(stage%8),stage%3!=0,stage%5==0,
        double(stage%5)/4,double(stage%3)/2};
      CHECK(a.setReverb(settings)&&b.setReverb(settings));
      if(stage==5){a.noteOff(69);b.noteOff(69);}
      if(stage==11||stage==20){a.panic();a.panic();b.panic();b.panic();}
      if(stage==12){a.noteOn(60,127);b.noteOn(60,127);}
      render(a,whole,4099);render(b,parts,7);
      CHECK(whole.left==parts.left&&whole.right==parts.right);
    }
  }
}
void testNoCallbackNewDelete() {
  SpuInstrument synth;
  std::array<float,1024> left{},right{};
  gAllocations=gFrees=0;
  gTrackAllocations=true;
  bool prepared=true,reverb=true;
  for (double rate : {44100.,48000.,96000.,8000.}) {
    prepared=synth.prepare(rate)&&prepared;
    for (int i=0; i<240; ++i) {
      synth.noteOn(i%128,1+i%127,i%16);
      synth.setPitchBend((i*797)%16384,i%16);
      synth.setSustain(i%3!=0,i%16);
      synth.setEnvelope({static_cast<std::uint16_t>((i*65521)^0x1327),
        static_cast<std::uint16_t>((i*27847)^0xa875)},i%3!=0);
      reverb=synth.setReverb({static_cast<ReverbPreset>(i%8),i%5!=0,i%3==0,
        double(i%101)/100.,.5})&&reverb;
      synth.render(left.data(),right.data(),1024);
      if (i%3==0) synth.noteOff(i%128,i%16);
      if (i%11==0) synth.allNotesOff(i%16);
      if (i%29==0) {synth.panic();synth.panic();}
      if (i%37==0) synth.resetControllers(i%16);
    }
    synth.allNotesOff();synth.resetControllers();
    for (int i=0; i<64; ++i) synth.render(left.data(),right.data(),1024);
  }
  gTrackAllocations=false;
  CHECK(prepared && reverb && synth.healthy());
  CHECK(gAllocations==0 && gFrees==0);
}
void word(FILE* file,std::uint32_t value,int count) {
  for (int i=0; i<count; ++i) std::fputc(static_cast<int>((value>>(8*i))&255),file);
}
void writeDemo(const char* path) {
  SpuInstrument synth;
  CHECK(synth.prepare(48000));
  Stereo pcm(48000*3);
  synth.noteOn(60,110); synth.noteOn(64,110); synth.noteOn(67,110);
  synth.render(pcm.left.data(),pcm.right.data(),48000*2);
  synth.allNotesOff();
  synth.render(pcm.left.data()+96000,pcm.right.data()+96000,48000);
  FILE* file=std::fopen(path,"wb"); CHECK(file);
  const auto bytes=static_cast<std::uint32_t>(pcm.left.size()*4);
  std::fwrite("RIFF",1,4,file); word(file,36+bytes,4); std::fwrite("WAVEfmt ",1,8,file);
  word(file,16,4); word(file,1,2); word(file,2,2); word(file,48000,4);
  word(file,48000*4,4); word(file,4,2); word(file,16,2);
  std::fwrite("data",1,4,file); word(file,bytes,4);
  for (std::size_t i=0; i<pcm.left.size(); ++i) {
    word(file,static_cast<std::uint16_t>(std::lround(pcm.left[i]*32767)),2);
    word(file,static_cast<std::uint16_t>(std::lround(pcm.right[i]*32767)),2);
  }
  CHECK(std::fclose(file)==0);
}
}
int main(int argc,char** argv) {
  testRatesPitchAndRelease(); testPartitionInvariance();
  testPolyphonyChannelsAndPanic(); testInvalidRatesAndInstanceIsolation();
  testMidiOffset(); testNoCallbackNewDelete();
  testPitchBend();testSustainAndAllNotesOff();testDeferredVoiceAllocation();
  testControllerResetPanicAndInvalid();testControllerPartitionInvariance();
  testEnvelopeDefaultsAndOwnership();testLiveEnvelopeStages();testDeferredEnvelopeAndNextNote();
  testEnvelopePhaseAndExplicitApply();testEnvelopePolyphonyPedalAndRelease();testEnvelopePartitionInvariance();
  testReverbDefaultsAndValidation();testReverbPresetsAndStereoReturns();testReverbBypassDecayAndToneSend();
  testReverbOffPanicAndPrepare();testOneShotReverbSendRestored();testReverbPartitionInvariance();
  if (argc==3 && std::strcmp(argv[1],"--wav")==0) writeDemo(argv[2]);
  std::puts("PASS: Native SPU pitch/release/polyphony/channels/panic/partition/isolation/rates/MIDI offsets; per-channel bend/sustain/reset/retrigger/deferred-steal; live/deferred ADSR; all reverb presets/stereo/bypass/Tone-send/one-shot/clear; zero callback C++ new/delete after cache priming");
  return 0;
}
