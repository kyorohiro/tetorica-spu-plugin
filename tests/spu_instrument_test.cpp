// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
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
void testNoCallbackNewDelete() {
  SpuInstrument synth;
  std::array<float,1024> left{},right{};
  gAllocations=gFrees=0;
  gTrackAllocations=true;
  bool prepared=true;
  for (double rate : {44100.,48000.,96000.,8000.}) {
    prepared=synth.prepare(rate)&&prepared;
    for (int i=0; i<240; ++i) {
      synth.noteOn(i%128,1+i%127,i%16);
      synth.render(left.data(),right.data(),1024);
      if (i%3==0) synth.noteOff(i%128,i%16);
      if (i%11==0) synth.allNotesOff(i%16);
      if (i%29==0) synth.panic();
    }
    synth.allNotesOff();
    for (int i=0; i<64; ++i) synth.render(left.data(),right.data(),1024);
  }
  gTrackAllocations=false;
  CHECK(prepared && synth.healthy());
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
  if (argc==3 && std::strcmp(argv[1],"--wav")==0) writeDemo(argv[2]);
  std::puts("PASS: Native SPU pitch/release/polyphony/channels/panic/partition/isolation/rates/MIDI offsets; zero callback C++ new/delete after cache priming");
  return 0;
}
