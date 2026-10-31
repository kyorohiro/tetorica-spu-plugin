// SPDX-License-Identifier: BSD-3-Clause
// Shared offline PCM assertions for actual AU/VST3 bundles, without test hooks.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace tetorica::test {
struct Midi { int status, data1, data2, offset=0; };
inline Midi on(int channel=0,int note=60,int offset=0) { return {0x90|channel,note,127,offset}; }
inline Midi off(int channel=0,int note=60,int offset=0) { return {0x80|channel,note,0,offset}; }
inline Midi cc(int number,int value,int channel=0,int offset=0) { return {0xb0|channel,number,value,offset}; }
inline Midi bend(int raw,int channel=0,int offset=0) { return {0xe0|channel,raw&127,raw>>7,offset}; }
inline void check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
inline double maximum(const std::vector<float>& audio) {
  double result=0;for(float value:audio)result=std::max(result,std::abs(double(value)));return result;
}
inline void concat(std::vector<float>& a,const std::vector<float>& b) { a.insert(a.end(),b.begin(),b.end()); }
inline double delta(const std::vector<float>& a,const std::vector<float>& b) {
  check(a.size()==b.size(),"Controller comparison size mismatch");
  double result=0;for(std::size_t i=0;i<a.size();++i)result=std::max(result,std::abs(double(a[i])-b[i]));return result;
}
inline double frequency(const std::vector<float>& audio,double rate) {
  int cycles=0;double first=0,last=0;
  for(std::size_t i=1025;i<audio.size();++i)
    if(audio[i-1]<=0&&audio[i]>0) {
      const double at=double(i)-audio[i]/double(audio[i]-audio[i-1]);
      if(!cycles++)first=at;
      last=at;
    }
  check(cycles>5,"Too few cycles for controller pitch measurement");
  return (cycles-1)*rate/(last-first);
}
template<class Instance> std::vector<float> blocks(Instance& instance,int count) {
  std::vector<float> result;for(int i=0;i<count;++i)concat(result,instance.renderMidi({}));return result;
}
template<class A,class B> void equalBlocks(A& a,B& b,int count,const char* message) {
  for(int i=0;i<count;++i)check(delta(a.renderMidi({}),b.renderMidi({}))<1e-7,message);
}

// create() returns an already-active fresh instance. The adapters translate this
// byte-level timeline through the real host API (VST3 IMidiMapping parameters,
// AU MusicDeviceMIDIEvent), never a production testing entry point.
template<class Factory> void checkControllers(Factory create,double rate,const char* format) {
  const int releaseBlocks=static_cast<int>(2*rate/256)+2;
  for(int raw:{0,8192,16383}) {
    auto instance=create();
    instance->renderMidi({bend(raw,15),on(15)});
    const double hz=frequency(blocks(*instance,64),rate);
    const double semitones=(raw-8192)*2./8192.;
    check(std::abs(hz-261.625565*std::pow(2.,semitones/12.))<1.,
      "Actual bundle MIDI bend endpoints/center have wrong frequency");
  }
  // Both changes must survive a single VST3 parameter queue. Compare the full
  // timeline against short host blocks, then check a one-sample bend excursion
  // preserves a nonzero held waveform rather than restarting phase/envelope.
  {
    auto whole=create(),split=create();whole->renderMidi({on()});split->renderMidi({on()});
    equalBlocks(*whole,*split,16,"Controller warmup diverged");
    auto a=whole->renderMidi({bend(12288,0,64),bend(4096,0,192)});
    auto b=split->renderMidi({},64);
    concat(b,split->renderMidi({bend(12288)},128));
    concat(b,split->renderMidi({bend(4096)},64));
    check(delta(a,b)<1e-7,"Multiple bend points lost offsets or changed with block partitioning");
    equalBlocks(*whole,*split,8,"Bent held-note timeline diverged after split blocks");
    auto unchanged=create(),excursion=create();
    unchanged->renderMidi({on()});excursion->renderMidi({on()});
    equalBlocks(*unchanged,*excursion,16,"No-KON bend reference diverged");
    a=excursion->renderMidi({bend(16383,0,73),bend(8192,0,74)});
    b=unchanged->renderMidi({});
    check(delta({a.begin(),a.begin()+73},{b.begin(),b.begin()+73})<1e-7,
      "Bend changed PCM before its offset");
    check(maximum({b.begin()+73,b.begin()+81})>.005&&
      delta({a.begin()+73,a.begin()+81},{b.begin()+73,b.begin()+81})<.003,
      "Held bend restarted sample phase/envelope instead of updating pitch");
  }
  // A pedal-down/up pair in one block must defer NoteOff to the up offset.
  {
    auto pedal=create(),reference=create();pedal->renderMidi({on()});reference->renderMidi({on()});
    equalBlocks(*pedal,*reference,16,"Sustain warmup diverged");
    const auto a=pedal->renderMidi({cc(64,64,0,32),off(0,60,64),cc(64,63,0,192)});
    const auto b=reference->renderMidi({off(0,60,192)});
    check(delta(a,b)<1e-7,"CC64 threshold/down-up offsets did not defer NoteOff");
    equalBlocks(*pedal,*reference,releaseBlocks,"Pedal-up release differs from timed NoteOff");
    check(maximum(pedal->renderMidi({}))<1e-7,"Pedal release left a stuck note");
  }
  {
    auto deferred=create(),reference=create();deferred->renderMidi({on()});reference->renderMidi({on()});
    equalBlocks(*deferred,*reference,16,"Deferred controller warmup diverged");
    check(delta(deferred->renderMidi({bend(12288,0,513)}),reference->renderMidi({}))<1e-7,
      "Future-offset bend started in the first block");
    check(delta(deferred->renderMidi({}),reference->renderMidi({}))<1e-7,
      "Future-offset bend started in the second block");
    check(delta(deferred->renderMidi({}),reference->renderMidi({bend(12288,0,1)}))<1e-7,
      "Future-offset bend was lost or moved across host blocks");
    equalBlocks(*deferred,*reference,8,"Deferred bend changed the later PCM timeline");
  }
  // An unused channel's bend/pedal cannot change another channel's sound.
  {
    auto isolated=create(),reference=create();
    isolated->renderMidi({bend(16383,15),cc(64,127,15),on(0)});
    reference->renderMidi({on(0)});
    equalBlocks(*isolated,*reference,16,"Channel15 controller affected channel0 held note");
    check(delta(isolated->renderMidi({off(0)}),reference->renderMidi({off(0)}))<1e-7,
      "Other channel's pedal held a released note");
    equalBlocks(*isolated,*reference,releaseBlocks,"Channel-isolated release diverged");
  }
  // CC123 respects the pedal, while CC121 releases only deferred keys and keeps
  // physically held keys. Compare against explicit NoteOff reference timelines.
  {
    auto allOff=create(),reference=create();
    allOff->renderMidi({cc(64,127,15),on(15)});reference->renderMidi({on(15)});
    equalBlocks(*allOff,*reference,16,"CC123 reference warmup diverged");
    check(delta(allOff->renderMidi({cc(123,0,15,41)}),reference->renderMidi({}))<1e-7,
      "All Notes Off ignored the sustain pedal");
    equalBlocks(*allOff,*reference,12,"CC123 sustained note decayed early");
    check(delta(allOff->renderMidi({cc(64,0,15,37)}),reference->renderMidi({off(15,60,37)}))<1e-7,
      "CC123 pedal-up did not release at its offset");
    equalBlocks(*allOff,*reference,releaseBlocks,"CC123 release tail diverged");
  }
  {
    auto reset=create(),reference=create();
    reset->renderMidi({bend(12288,3),cc(64,127,3),on(3,60),on(3,67)});
    reference->renderMidi({bend(12288,3),on(3,60),on(3,67)});
    equalBlocks(*reset,*reference,16,"CC121 warmup diverged");
    check(delta(reset->renderMidi({off(3,60)}),reference->renderMidi({}))<1e-7,
      "Pedal did not preserve deferred key before reset");
    check(delta(reset->renderMidi({cc(121,0,3,43)}),
      reference->renderMidi({cc(121,0,3,43),off(3,60,43)}))<1e-7,
      "CC121 did not center bend/release deferred key at its offset");
    equalBlocks(*reset,*reference,releaseBlocks,"CC121 changed held/release PCM ordering");
    check(maximum(reset->renderMidi({}))>1e-4,"CC121 stopped a physically held key");
  }
  // Panic silences globally while preserving live controllers. Live values are
  // excluded from project data. Lifecycle reset and successful state restore
  // instead return bend/pedal to their initial values.
  {
    auto panic=create();const auto original=panic->savedBytes();
    panic->renderMidi({bend(12288,15),cc(64,127,15),on(15)});
    blocks(*panic,16);panic->renderMidi({cc(120,0,0,19)});
    check(maximum(blocks(*panic,8))<1e-12,"CC120 did not globally silence another channel");
    panic->renderMidi({on(15)});
    check(std::abs(frequency(blocks(*panic,64),rate)-277.18263)<1.,"Panic reset live bend");
    panic->renderMidi({off(15)});
    blocks(*panic,releaseBlocks);
    check(maximum(panic->renderMidi({}))>1e-4,"Panic reset the sustain pedal");
    check(panic->savedBytes()==original,"Live MIDI controllers leaked into preset state");
    panic->resetForMidiTest();
    check(maximum(blocks(*panic,4))<1e-12,"Lifecycle reset retained a sustained note");
    panic->renderMidi({on(15)});
    check(std::abs(frequency(blocks(*panic,64),rate)-261.625565)<1.,"Lifecycle reset retained bend");
    panic->renderMidi({off(15)});
    blocks(*panic,releaseBlocks);
    check(maximum(panic->renderMidi({}))<1e-7,"Lifecycle reset retained pedal");
    panic->renderMidi({bend(16383,15),cc(64,127,15),on(15)});
    panic->renderMidi({off(15)});
    auto fresh=create();fresh->restoreForMidiTest(original);panic->restoreForMidiTest(original);
    check(delta(panic->renderMidi({on(15,60,37)}),fresh->renderMidi({on(15,60,37)}))<1e-7,
      "Host restore retained controllers/held notes or lost first fresh NoteOn");
    equalBlocks(*panic,*fresh,16,"Host restore controller reset PCM differs from fresh instance");
    panic->renderMidi({off(15)});fresh->renderMidi({off(15)});
    equalBlocks(*panic,*fresh,releaseBlocks,"Host restore retained a sustain pedal");
  }
  {
    auto overflow=create();overflow->renderMidi({bend(16383,15),cc(64,127,15),on(15)});
    std::vector<Midi> flood(2049,on(15));overflow->renderMidi(flood);
    check(maximum(blocks(*overflow,4))<1e-12,"Controller overflow did not silence safely");
    overflow->renderMidi({on(15)});
    check(std::abs(frequency(blocks(*overflow,64),rate)-261.625565)<1.,"Overflow retained bend");
    overflow->renderMidi({off(15)});blocks(*overflow,releaseBlocks);
    check(maximum(overflow->renderMidi({}))<1e-7,"Overflow retained pedal");
  }
  {
    auto lifecycle=create();lifecycle->renderMidi({bend(16383,15),cc(64,127,15),on(15)});
    lifecycle->renderMidi({off(15)});lifecycle->reactivateForMidiTest();
    check(maximum(blocks(*lifecycle,4))<1e-12,"Deactivate/reactivate retained sustained audio");
    lifecycle->renderMidi({on(15)});
    check(std::abs(frequency(blocks(*lifecycle,64),rate)-261.625565)<1.,
      "Deactivate/reactivate retained bend");
    lifecycle->renderMidi({off(15)});blocks(*lifecycle,releaseBlocks);
    check(maximum(lifecycle->renderMidi({}))<1e-7,"Deactivate/reactivate retained pedal");
  }
  {
    auto restored=create(),fresh=create();const auto original=restored->savedBytes();
    restored->renderMidi({bend(16383,15,513),cc(64,127,15,513)});
    restored->restoreForMidiTest(original);fresh->restoreForMidiTest(original);
    check(delta(restored->renderMidi({on(15,60,37)}),fresh->renderMidi({on(15,60,37)}))<1e-7,
      "Host restore lost a fresh NoteOn while discarding carried controllers");
    equalBlocks(*restored,*fresh,20,"Pre-restore future bend/pedal survived into later blocks");
    restored->renderMidi({off(15)});fresh->renderMidi({off(15)});
    equalBlocks(*restored,*fresh,releaseBlocks,"Pre-restore carried pedal later stranded a fresh note");
    check(maximum(restored->renderMidi({}))<1e-7,"Host restore retained future pedal state");
  }
  // Deliberately author a long release in our original demo fixture. Bend must
  // retune a released voice too, without a KON/phase reset or a new held note.
  {
    std::ifstream input(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
    check(input.good(),"Cannot read controller release-tail VAB fixture");
    std::vector<std::uint8_t> bank{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
    check(bank.size()>0x834,"Controller VAB fixture is truncated");
    bank[0x820+18]=static_cast<std::uint8_t>((bank[0x820+18]&0xe0)|31);
    auto tail=create(),reference=create();tail->importForMidiTest(bank);reference->importForMidiTest(bank);
    tail->renderMidi({on(15)});reference->renderMidi({on(15)});
    equalBlocks(*tail,*reference,16,"Release-tail VAB warmup diverged");
    tail->renderMidi({off(15)});reference->renderMidi({off(15)});
    equalBlocks(*tail,*reference,4,"Long-release voice ended unexpectedly");
    const auto before=blocks(*tail,64),beforeReference=blocks(*reference,64);
    check(delta(before,beforeReference)<1e-7&&maximum(before)>.001,
      "Release-tail bend test did not contain a live released voice");
    auto baseline=reference->renderMidi({});
    int at=40;for(int i=41;i<192;++i)if(std::abs(baseline[i])>std::abs(baseline[at]))at=i;
    const auto excursion=tail->renderMidi({bend(16383,15,at),bend(8192,15,at+1)});
    check(delta({excursion.begin(),excursion.begin()+at},{baseline.begin(),baseline.begin()+at})<1e-7&&
      delta({excursion.begin()+at,excursion.begin()+at+8},{baseline.begin()+at,baseline.begin()+at+8})<maximum(baseline)*.08,
      "Bend retriggered a release tail or changed its pre-offset PCM");
    tail->renderMidi({bend(12288,15)});
    const auto after=blocks(*tail,64);
    check(std::abs(frequency(after,rate)/frequency(before,rate)-std::pow(2.,1./12))<.003,
      "Bend failed to retune a released voice");
  }
  std::cout<<"PASS "<<format<<" controllers sampleRate="<<rate
    <<" bend14Endpoints=true multiPointOffsets=true splitBlocks=true noRetrigger=true"
    <<" sustain64_63=true channel15Isolation=true CC123Pedal=true CC121HeldDeferred=true"
    <<" panicPreservesControllers=true resetRestoreOverflowClear=true liveStateExcluded=true"
    <<" deferredController=true reactivateReset=true releaseTailRetunedNoKON=true"
    <<" restoreDiscardsFutureControllers=true\n";
}
} // namespace tetorica::test
