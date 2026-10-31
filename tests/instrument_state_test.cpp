// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "InstrumentState.h"
#include "SpuEngineExchange.h"
#include "VabLoader.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <type_traits>

#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
namespace {
thread_local bool gTrack=false,gFailNext=false;
thread_local std::size_t gNew=0,gDelete=0;
void* allocate(std::size_t n){
  if(gFailNext){gFailNext=false;throw std::bad_alloc();}
  if(gTrack)++gNew;
  if(auto* p=std::malloc(n?n:1))return p;
  throw std::bad_alloc();
}
void release(void* p)noexcept{if(gTrack&&p)++gDelete;std::free(p);}
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
using Def=InstrumentDefinition;
using Bytes=std::vector<std::uint8_t>;
Bytes fixture(){
  std::ifstream file(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
  CHECK(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
template<class F>void rejects(F&& f){try{f();}catch(const std::exception&){return;}CHECK(false);}
void word(Bytes& bytes,std::size_t at,std::uint32_t value){
  for(unsigned i=0;i<4;++i)bytes[at+i]=static_cast<std::uint8_t>(value>>(i*8));
}
SavedInstrumentState decode(const Bytes& bytes){return decodeState({bytes.data(),bytes.size()});}
bool sameEnvelope(EnvelopeSettings a,EnvelopeSettings b){
  return a.custom.adsr1==b.custom.adsr1&&a.custom.adsr2==b.custom.adsr2&&a.enabled==b.enabled&&a.live==b.live;
}
bool sameReverb(ReverbSettings a,ReverbSettings b){
  return a.preset==b.preset&&a.enabled==b.enabled&&a.useToneSend==b.useToneSend&&
    a.volumeLeft==b.volumeLeft&&a.volumeRight==b.volumeRight;
}
std::size_t bankLength(const Bytes& bytes){
  std::uint32_t result=0;for(unsigned i=0;i<4;++i)result|=std::uint32_t(bytes.at(28+i))<<(i*8);return result;
}
Bytes oldSchema(Bytes bytes,unsigned version){
  bytes.resize(bankLength(bytes)+(version==1?40:version==2?48:72));word(bytes,8,version);
  word(bytes,12,static_cast<std::uint32_t>(bytes.size()));return bytes;
}
struct Stereo{std::vector<float> left,right;explicit Stereo(std::size_t n):left(n),right(n){}};
void render(SpuInstrument& engine,Stereo& out){engine.render(out.left.data(),out.right.data(),static_cast<int>(out.left.size()));CHECK(engine.healthy());}
float peak(const Stereo& out){float p=0;for(auto v:out.left)p=std::max(p,std::abs(v));return p;}
void codec(){
  static_assert(!std::is_copy_constructible<Def>::value&&!std::is_copy_assignable<Def>::value);
  static_assert(!std::is_move_constructible<Def>::value&&!std::is_default_constructible<Def>::value);
  static_assert(std::is_same<decltype(std::declval<const Def&>().bankBytes()),const Bytes&>::value);
  const auto builtin=Def::builtin();
  CHECK(builtin->kind()==Def::Kind::Builtin&&builtin->bankBytes().empty()&&builtin->program()==0&&builtin->tone()==0);
  const auto encoded=encodeState(builtin,37.5);
  CHECK(encoded.size()==80&&std::equal(kStateMagic.begin(),kStateMagic.end(),encoded.begin()));
  CHECK(encoded[8]==5&&encoded[12]==80&&encoded[16]==0);
  CHECK(encoded[37]==0xc0&&encoded[38]==0x42&&encoded[39]==0x40); //LE binary64 37.5.
  auto restored=decode(encoded);CHECK(restored.gainPercent==37.5&&restored.definition==builtin);
  CHECK(sameEnvelope(restored.envelope,{})&&sameReverb(restored.reverb,{}));
  auto bytes=fixture();const auto original=bytes;
  auto selected=Def::fromVab(bytes,3,1);std::fill(bytes.begin(),bytes.end(),0);
  CHECK(selected->bankBytes()==original&&selected->program()==3&&selected->tone()==1);
  auto flat=Def::fromVabFlatIndex(original,1);
  CHECK(flat->bankBytes()==selected->bankBytes()&&flat->program()==3&&flat->tone()==1);
  auto moved=original;const auto previousAddress=reinterpret_cast<std::uintptr_t>(moved.data());
  const auto independent=Def::fromVab(std::move(moved),3,0);
  CHECK(reinterpret_cast<std::uintptr_t>(independent->bankBytes().data())!=previousAddress);
  CHECK(independent->bankBytes()==original); //Never dereference the destroyed input alias.
  for(double value:{0.,37.,100.}){
    const auto wire=encodeState(selected,value);const auto read=decode(wire);
    CHECK(read.gainPercent==value&&read.definition->bankBytes()==original&&read.definition->program()==3&&read.definition->tone()==1);
    CHECK(encodeState(read.definition,read.gainPercent)==wire);
  }
  for(unsigned flags=0;flags<4;++flags){
    EnvelopeSettings settings{{0xa937,0xd7bb},bool(flags&1),bool(flags&2)};
    const auto wire=encodeState(selected,37,settings);const auto read=decode(wire);
    CHECK(wire.size()==original.size()+80&&sameEnvelope(settings,read.envelope));
    CHECK(encodeState(read.definition,read.gainPercent,read.envelope)==wire);
    CHECK(read.definition->bankBytes()==original);
    // v1 has no authored override; migration must never infer custom values
    // from the new extension or overwrite the original bank's ADSR metadata.
    const auto old=oldSchema(wire,1);const auto migrated=decode(old);
    CHECK(migrated.definition->bankBytes()==original&&migrated.gainPercent==37&&sameEnvelope(migrated.envelope,{}));
    CHECK(encodeState(migrated.definition,migrated.gainPercent,migrated.envelope).size()==wire.size());
  }
  const EnvelopeSettings authored{{0xa937,0xd7bb},true,false};
  for(unsigned preset=0;preset<8;++preset)for(unsigned flags=0;flags<4;++flags){
    const ReverbSettings effects{static_cast<ReverbPreset>(preset),bool(flags&1),bool(flags&2),.375,.8125};
    const auto wire=encodeState(selected,37,authored,effects);const auto read=decode(wire);
    CHECK(sameEnvelope(read.envelope,authored)&&sameReverb(read.reverb,effects));
    CHECK(read.definition->bankBytes()==original&&encodeState(read.definition,37,read.envelope,read.reverb)==wire);
    const auto migrated=decode(oldSchema(wire,2));
    CHECK(sameEnvelope(migrated.envelope,authored)&&sameReverb(migrated.reverb,{}));
    CHECK(migrated.definition->bankBytes()==original);
    const auto v1=decode(oldSchema(wire,1));
    CHECK(sameEnvelope(v1.envelope,{})&&sameReverb(v1.reverb,{}));
  }
  for(auto invalid:{ReverbSettings{static_cast<ReverbPreset>(8)},
      ReverbSettings{ReverbPreset::Room,true,false,-.1,.5},
      ReverbSettings{ReverbPreset::Room,false,true,.1,1.1},
      ReverbSettings{ReverbPreset::Room,true,false,std::numeric_limits<double>::quiet_NaN(),.5},
      ReverbSettings{ReverbPreset::Room,true,false,.5,std::numeric_limits<double>::infinity()}})
    rejects([&]{encodeState(selected,37,authored,invalid);});
  auto sourceReserved=original;sourceReserved[0x820+19]|=0x20;
  const auto sourceState=decode(encodeState(Def::fromVab(sourceReserved,3,0),37));
  CHECK(sourceState.definition->bankBytes()==sourceReserved); //Source bit13 remains legal and exact.
  EnvelopeSettings reserved;reserved.custom.adsr2|=0x2000;
  rejects([&]{encodeState(builtin,37,reserved);});
  const auto oldBuiltin=decode(oldSchema(encoded,1));
  CHECK(oldBuiltin.definition==builtin&&sameEnvelope(oldBuiltin.envelope,{}));
  const auto shot=decode(encodeState(Def::fromVab(original,11,0),99.));
  CHECK(shot.definition->program()==11&&shot.definition->tone()==0);
  rejects([&]{Def::fromVab(original,0,0);});rejects([&]{Def::fromVab(original,3,16);});
  rejects([&]{Def::fromVabFlatIndex(original,3);});
  rejects([&]{encodeState(std::shared_ptr<const Def>{},25);});
  for(double value:{-1.,101.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
    rejects([&]{encodeState(builtin,value);});
}
void malformed(){
  const auto good=encodeState(Def::fromVab(fixture(),3,0),25.);
  for(std::size_t n=0;n<80;++n)rejects([&]{decodeState({good.data(),n});});
  rejects([&]{decodeState({nullptr,40});});rejects([&]{decodeState({nullptr,0});});
  rejects([&]{decodeState({good.data(),kMaxStateBytes+1});});
  for(const auto at:{0u,7u,8u,12u,16u,20u,24u,28u}){
    auto bad=good;word(bad,at,0xffffffffu);rejects([&]{decode(bad);});
  }
  auto trailing=good;trailing.push_back(0);rejects([&]{decode(trailing);});
  auto truncated=good;truncated.pop_back();rejects([&]{decode(truncated);});
  for(auto flags:{4u,8u,0xffffffffu}){auto bad=good;word(bad,44+bankLength(bad),flags);rejects([&]{decode(bad);});}
  {auto bad=good;bad[43+bankLength(bad)]|=0x20;rejects([&]{decode(bad);});} //Custom ADSR2 bit13.
  {auto bad=good;word(bad,8,1);rejects([&]{decode(bad);});} //v1 cannot carry a v2 tail.
  {auto bad=oldSchema(good,1);word(bad,8,2);rejects([&]{decode(bad);});} //v2 requires all eight extension bytes.
  for(auto flags:{4u,8u,0xffffffffu}){auto bad=good;word(bad,52+bankLength(bad),flags);rejects([&]{decode(bad);});}
  for(auto preset:{8u,0xffffffffu}){auto bad=good;word(bad,48+bankLength(bad),preset);rejects([&]{decode(bad);});}
  for(auto at:{56u,64u})for(auto bits:{0x7ff0000000000000ull,0x7ff8000000000000ull,0xbff0000000000000ull,0x3ff0000000000001ull}){
    auto bad=good;for(unsigned i=0;i<8;++i)bad[at+bankLength(bad)+i]=static_cast<std::uint8_t>(bits>>(i*8));
    rejects([&]{decode(bad);});
  }
  {auto bad=oldSchema(good,2);word(bad,8,3);rejects([&]{decode(bad);});}
  for(auto n:{0u,1u,1048577u}){auto bad=good;word(bad,28,n);rejects([&]{decode(bad);});}
  for(const auto field:{20u,24u,28u}){
    auto bad=encodeState(Def::builtin(),25);word(bad,field,1);rejects([&]{decode(bad);});
  }
  for(const auto sample:{0u,255u}){
    auto bad=good;bad[32+0x820+22]=static_cast<std::uint8_t>(sample);rejects([&]{decode(bad);});
  }
  auto bad=good;bad[32+0xe20+16]=0x50;rejects([&]{decode(bad);}); //Selected predictor.
  bad=good;word(bad,20,11);word(bad,24,1);rejects([&]{decode(bad);});
  for(auto bits:{0x7ff0000000000000ull,0x7ff8000000000000ull,0xbff0000000000000ull,0x4059400000000000ull}){
    bad=good;for(unsigned i=0;i<8;++i)bad[32+bankLength(bad)+i]=static_cast<std::uint8_t>(bits>>(i*8));
    rejects([&]{decode(bad);});
  }
}
void pcmCycles(){
  static_assert(std::is_same<decltype(std::declval<const Def&>().pcmCycle()),const PcmCycle&>::value);
  const EnvelopeSettings envelope{{0xa937,0xd7bb},false,false};
  const ReverbSettings effects{ReverbPreset::Hall,true,false,.375,.8125};
  for(auto shape:{WaveformShape::Sine,WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square}) {
    auto cycle=makePcmCycle(shape);const auto original=cycle;
    const auto definition=Def::fromPcmCycle(cycle);cycle.fill(0);
    CHECK(definition->kind()==Def::Kind::PcmCycle&&definition->bankBytes().empty());
    CHECK(definition->program()==0&&definition->tone()==0&&definition->pcmCycle()==original);
    const auto wire=encodeState(definition,37,envelope,effects);const auto read=decode(wire);
    CHECK(wire.size()==528&&wire[8]==5&&wire[16]==2&&bankLength(wire)==448);
    CHECK(read.definition->pcmCycle()==original&&sameEnvelope(read.envelope,envelope)&&sameReverb(read.reverb,effects));
    CHECK(encodeState(read.definition,read.gainPercent,read.envelope,read.reverb)==wire);
    auto bad=wire;word(bad,8,3);rejects([&]{decode(bad);}); //Older versions do not know kind2.
    for(auto at:{20u,24u}){bad=wire;word(bad,at,1);rejects([&]{decode(bad);});}
    for(auto size:{0u,447u,449u}) {
      bad=wire;bad.resize(size+80);word(bad,28,size);word(bad,12,static_cast<std::uint32_t>(bad.size()));
      rejects([&]{decode(bad);});
    }
    for(auto bits:{0x7f800000u,0x7fc00000u,0xbf800001u,0x3f800001u}) {
      bad=wire;word(bad,32+4*111,bits);rejects([&]{decode(bad);});
    }
  }
  PcmCycle custom{};custom[0]=1;custom[1]=-1;custom[2]=-.0f;custom[111]=.125f;
  const auto bytes=encodeState(Def::fromPcmCycle(custom),37);
  CHECK(bytes[34]==0x80&&bytes[35]==0x3f&&bytes[39]==0xbf); //Canonical binary32 LE endpoints.
  CHECK(bytes[43]==0x80); //Keep signed zero as authored, rather than normalizing samples.
  CHECK(decode(bytes).definition->pcmCycle()==custom);
  for(float value:{-1.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    auto invalid=custom;invalid[3]=value;rejects([&]{Def::fromPcmCycle(invalid);});
  }
  // Schema3 reproduces the preceding complete envelope/reverb tuple, while
  // source kind0/1 retain their previous meaning and exact source bytes.
  const auto imported=Def::fromVab(fixture(),3,1);
  const auto old=decode(oldSchema(encodeState(imported,37,envelope,effects),3));
  CHECK(old.definition->bankBytes()==imported->bankBytes()&&sameEnvelope(old.envelope,envelope)&&sameReverb(old.reverb,effects));
  // A prepared custom cycle enters the same transactional exchange and sounds
  // on the immediately following block, matching an independent preparation.
  const auto def=Def::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  SpuEngineExchange exchange;auto independent=prepareDefinition(*def);CHECK(independent->prepare(48000));
  exchange.commitPrepared(prepareDefinition(*def),def);CHECK(exchange.snapshotDefinition()==def&&exchange.beginBlock(48000));
  Stereo a(8192),b(8192);exchange.instrument().noteOn(60,127);independent->noteOn(60,127);
  render(exchange.instrument(),a);render(*independent,b);CHECK(a.left==b.left&&a.right==b.right&&peak(a)>.01f);
}
void noiseSettings(){
  const auto builtin=Def::builtin(),vab=Def::fromVab(fixture(),3,1);
  const auto pcm=Def::fromPcmCycle(makePcmCycle(WaveformShape::Saw));
  const EnvelopeSettings envelope{{0xa937,0xd7bb},true,false};
  const ReverbSettings effects{ReverbPreset::Hall,true,false,.375,.8125};
  for(const auto& definition:{builtin,vab,pcm})for(bool enabled:{false,true})for(unsigned clock:{0u,48u,63u}) {
    const NoiseSettings generator{enabled,clock};
    const auto bytes=encodeState(definition,37,envelope,effects,generator);const auto read=decode(bytes);
    CHECK(read.noise.enabled==enabled&&read.noise.clock==clock&&read.gainPercent==37);
    CHECK(sameEnvelope(read.envelope,envelope)&&sameReverb(read.reverb,effects));
    CHECK(read.definition->kind()==definition->kind()&&read.definition->bankBytes()==definition->bankBytes()&&
      read.definition->pcmCycle()==definition->pcmCycle());
    CHECK(encodeState(read.definition,37,read.envelope,read.reverb,read.noise)==bytes);
    const auto old4=decode(oldSchema(bytes,4));
    CHECK(!old4.noise.enabled&&old4.noise.clock==48&&sameEnvelope(old4.envelope,envelope)&&sameReverb(old4.reverb,effects));
    CHECK(old4.definition->pcmCycle()==definition->pcmCycle()&&old4.definition->bankBytes()==definition->bankBytes());
    for(auto flags:{2u,4u,0xffffffffu}) {auto bad=bytes;word(bad,72+bankLength(bad),flags);rejects([&]{decode(bad);});}
    for(auto invalid:{64u,0xffffffffu}) {auto bad=bytes;word(bad,76+bankLength(bad),invalid);rejects([&]{decode(bad);});}
    if(definition->kind()!=Def::Kind::PcmCycle)for(unsigned version:{1u,2u,3u}) {
      const auto old=decode(oldSchema(bytes,version));CHECK(!old.noise.enabled&&old.noise.clock==48);
    }
  }
  for(unsigned clock:{64u,0xffffffffu})rejects([&]{encodeState(pcm,37,{}, {},{true,clock});});
}
void immediateRestore(){
  const auto state=decode(encodeState(Def::fromVab(fixture(),3,1),37.));
  SpuEngineExchange exchange;
  auto fresh=prepareDefinition(*state.definition);CHECK(fresh->prepare(48000));
  const auto generation=exchange.commitPrepared(prepareDefinition(*state.definition),state.definition);
  CHECK(exchange.latestGeneration()==generation&&exchange.adoptedGeneration()==0);
  CHECK(exchange.snapshotDefinition()==state.definition);
  CHECK(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==generation);
  CHECK(exchange.snapshotDefinition()==state.definition);
  Stereo a(48000),b(48000);
  exchange.instrument().noteOn(60,127);fresh->noteOn(60,127);
  render(exchange.instrument(),a);render(*fresh,b);
  CHECK(a.left==b.left&&a.right==b.right&&peak(a)>.01f);
  // Even with no loader/collector, repeated synchronous restores create room
  // before committing, so the immediate first block never waits on a full ring.
  for(int i=0;i<12;++i){
    const auto def=i%2?state.definition:Def::builtin();
    const auto gen=exchange.commitPrepared(prepareDefinition(*def),def);
    CHECK(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==gen);
  }
}
void snapshotsAndAtomicFailure(){
  SpuEngineExchange exchange;
  CHECK(exchange.snapshotDefinition()->kind()==Def::Kind::Builtin);
  auto plain=Def::fromVab(fixture(),3,0),fine=Def::fromVab(fixture(),3,1);
  auto generation=exchange.beginRequest();
  CHECK(exchange.publish(generation,prepareDefinition(*plain),plain));
  CHECK(exchange.snapshotDefinition()==plain); //Saved before audio adoption.
  const auto latest=generation;
  rejects([&]{exchange.commitPrepared(nullptr,fine);});
  CHECK(exchange.latestGeneration()==latest&&exchange.snapshotDefinition()==plain);
  auto prepared=prepareDefinition(*fine);
  gFailNext=true;
  try{exchange.commitPrepared(std::move(prepared),fine);CHECK(false);}catch(const std::bad_alloc&){}
  CHECK(!gFailNext&&exchange.latestGeneration()==latest&&exchange.snapshotDefinition()==plain);
  CHECK(exchange.beginBlock(44100));CHECK(exchange.snapshotDefinition()==plain);
  auto pending=exchange.beginRequest();CHECK(exchange.publish(pending,prepareDefinition(*fine),fine));
  CHECK(exchange.snapshotDefinition()==fine);
  exchange.beginRequest(); //Cancellation invalidates pending, active stillplain.
  CHECK(exchange.snapshotDefinition()==plain);
  CHECK(!exchange.beginBlock(44100)&&exchange.snapshotDefinition()==plain);
  auto badRate=exchange.beginRequest();CHECK(exchange.publish(badRate,prepareDefinition(*fine),fine));
  CHECK(exchange.snapshotDefinition()==fine);
  CHECK(!exchange.beginBlock(std::numeric_limits<double>::quiet_NaN()));
  CHECK(exchange.snapshotDefinition()==plain); //Rejectedhostrate cannot persist the candidate.
  //Native-specific unsupported flag pattern passes pure codec but fails full
  //preparation; because no commit happened it cannot cancel an existing load.
  auto invalidBytes=fixture();invalidBytes[0xe20+17]=4;
  auto unsupported=Def::fromVab(std::move(invalidBytes),3,0);
  const auto before=exchange.latestGeneration();
  rejects([&]{prepareDefinition(*unsupported);});
  CHECK(exchange.latestGeneration()==before&&exchange.snapshotDefinition()==plain);
  auto raw=exchange.beginRequest();CHECK(exchange.publish(raw,std::make_unique<SpuInstrument>()));
  CHECK(!exchange.snapshotDefinition());CHECK(exchange.beginBlock(44100));CHECK(!exchange.snapshotDefinition());
}
void callbackAndDefinitionLifetime(){
  SpuEngineExchange exchange;
  auto first=Def::fromVab(fixture(),3,0);
  const std::weak_ptr<const Def> retiredDefinition=first;
  exchange.commitPrepared(prepareDefinition(*first),first);
  CHECK(exchange.beginBlock(48000));exchange.collectRetired();first.reset();
  auto next=Def::fromVab(fixture(),3,1);
  exchange.commitPrepared(prepareDefinition(*next),next);next.reset();
  CHECK(!retiredDefinition.expired());
  std::array<float,1024> left{},right{};
  gNew=gDelete=0;gTrack=true;
  const bool adopted=exchange.beginBlock(48000);
  exchange.instrument().noteOn(60,127);
  exchange.instrument().render(left.data(),right.data(),1024);
  exchange.instrument().allNotesOff();exchange.instrument().render(left.data(),right.data(),1024);
  gTrack=false;
  CHECK(adopted&&gNew==0&&gDelete==0&&!retiredDefinition.expired());
  exchange.collectRetired();CHECK(retiredDefinition.expired());
}
void loaderMetadata(){
  SpuEngineExchange exchange;VabLoader loader(exchange);
  const auto generation=loader.request(fixture(),1);
  for(int i=0;i<2000&&loader.status().state==VabLoader::State::Loading;++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(loader.status().state==VabLoader::State::Ready);
  const auto def=exchange.snapshotDefinition();
  CHECK(def&&def->kind()==Def::Kind::Vab&&def->program()==3&&def->tone()==1);
  CHECK(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==generation);
  loader.requestBuiltin();
  for(int i=0;i<2000&&loader.status().state==VabLoader::State::Loading;++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  CHECK(loader.status().state==VabLoader::State::Ready);
  CHECK(exchange.snapshotDefinition()->kind()==Def::Kind::Builtin);
}
void concurrentSnapshots(){
  SpuEngineExchange exchange;
  auto plain=Def::fromVab(fixture(),3,0),fine=Def::fromVab(fixture(),3,1);
  std::atomic<bool> stop{false};
  std::thread audio([&]{
    std::array<float,64> left{},right{};
    while(!stop.load(std::memory_order_acquire)){
      exchange.beginBlock(48000);exchange.instrument().render(left.data(),right.data(),64);
    }
  });
  for(int i=0;i<80;++i){
    const auto def=i%2?plain:fine;
    exchange.commitPrepared(prepareDefinition(*def),def);
    const auto snap=exchange.snapshotDefinition();
    CHECK(snap==def); //Logical accepteddefinition survives pending->active handoff.
    const auto wire=encodeState(snap,25.);CHECK(decode(wire).definition->bankBytes()==snap->bankBytes());
    exchange.collectRetired();
  }
  stop.store(true,std::memory_order_release);audio.join();exchange.collectRetired();
}
}
int main(){
  codec();malformed();pcmCycles();noiseSettings();immediateRestore();snapshotsAndAtomicFailure();
  callbackAndDefinitionLifetime();loaderMetadata();concurrentSnapshots();
  std::puts("PASS immutable VAB/PCM state, schema5 Noise/PCM/Reverb/ADSR, schema1/2/3/4 migration/source preservation, strict codec, immediate PCM restore, snapshots, failure atomicity, RT lifetime and concurrent collection");
}
