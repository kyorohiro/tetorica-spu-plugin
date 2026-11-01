// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SoftwarePcm.h"
#include "InstrumentState.h"
#include "SpuEngineExchange.h"
#include "SpuExportSource.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace {
thread_local bool tracking=false;
thread_local std::size_t allocations=0,largestAllocation=0;
void* allocate(std::size_t size){if(tracking){++allocations;largestAllocation=std::max(largestAllocation,size);}if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
}
void* operator new(std::size_t size){return allocate(size);}void* operator new[](std::size_t size){return allocate(size);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
using namespace tetorica;
using Def=InstrumentDefinition;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F action){bool threw=false;try{action();}catch(const std::exception&){threw=true;}check(threw,"invalid software PCM accepted");}
void word(std::vector<std::uint8_t>& bytes,std::size_t at,std::uint32_t value){for(unsigned i=0;i<4;++i)bytes.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));}
std::uint32_t word(const std::vector<std::uint8_t>& bytes,std::size_t at){std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes.at(at+i))<<(i*8);return value;}
SavedInstrumentState decode(const std::vector<std::uint8_t>& bytes){return decodeState({bytes.data(),bytes.size()});}
std::vector<std::uint8_t> encode(const SavedInstrumentState& state){return encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph);}
bool sameEndpoints(const MorphEndpoints& a,const MorphEndpoints& b){return !std::memcmp(a.a.data(),b.a.data(),sizeof(a.a))&&!std::memcmp(a.b.data(),b.b.data(),sizeof(a.b));}
SoftwarePcmSample source(std::size_t frames=4096){
  std::vector<std::int16_t> pcm(frames);
  for(std::size_t i=0;i<frames;++i)pcm[i]=static_cast<std::int16_t>(std::lround(20000*std::sin(i*6.283185307179586/112)));
  SoftwarePcmSample sample;sample.pcm=SoftwarePcmBuffer::copyOf(pcm);sample.loop=true;
  sample.name="Software source 原音";sample.volumeLeft=-.375;sample.volumeRight=.625;
  sample.envelope={0x000f,0x3fc0};sample.inspectorPitch=17;sample.reverb=true;
  return sample;
}
void buffersAndBase64(){
  static_assert(std::is_same<decltype(std::declval<const SoftwarePcmBuffer&>().samples()),const std::vector<std::int16_t>&>::value,"PCM must have no mutable accessor");
  std::vector<std::int16_t> external{-32768,-1,0,1,32767};
  auto buffer=SoftwarePcmBuffer::copyOf(external);const auto expected=external;external[0]=42;
  check(buffer->samples()==expected&&buffer->samples().data()!=external.data(),"public factory retained a mutable alias");
  std::vector<std::uint8_t> little{0,128,255,255,0,0,1,0,255,127};
  auto read=SoftwarePcmBuffer::fromLittleEndian({little.data(),little.size()});little[0]=99;
  check(read->samples()==expected,"PCM16LE decode or ownership is wrong");
  std::string text="prefix:";appendSoftwarePcmBase64(*buffer,text);
  check(text=="prefix:AID//wAAAQD/fw==","base64 signed/endian golden mismatch");
  check(decodeSoftwarePcmBase64(std::string_view(text).substr(7))->samples()==expected,"base64 signed roundtrip failed");
  for(std::size_t n=1;n<=9;++n){
    std::vector<std::int16_t> values(n);for(std::size_t i=0;i<n;++i)values[i]=static_cast<std::int16_t>(i*1234-5000);
    auto value=SoftwarePcmBuffer::copyOf(values);std::string encoded;appendSoftwarePcmBase64(*value,encoded);
    check(decodeSoftwarePcmBase64(encoded)->samples()==values,"base64 final-group boundary failed");
  }
  for(auto bad:{"","A","AA","AAA","AAAAA","AA==","AAAA","AAB=","AA=A","====","AA-_","AAA\n","AAA ","AAAA===="})rejects([&]{decodeSoftwarePcmBase64(bad);});
  rejects([&]{SoftwarePcmBuffer::copyOf({});});
  rejects([&]{SoftwarePcmBuffer::fromLittleEndian({nullptr,2});});
  const std::uint8_t byte=0;rejects([&]{SoftwarePcmBuffer::fromLittleEndian({&byte,1});});
  rejects([&]{SoftwarePcmBuffer::fromLittleEndian({&byte,kMaxSoftwarePcmBytes+2});});
}
void metadataAndSharing(){
  const auto sample=source();validateSoftwarePcmSample(sample);
  auto definition=Def::fromSoftwarePcmSample(sample);
  check(definition->kind()==Def::Kind::SoftwarePcm&&definition->softwarePcmSample().pcm==sample.pcm,"definition copied private PCM or changed kind");
  auto caller=sample;caller.name="changed";caller.volumeLeft=1;
  check(sameSoftwarePcmSample(definition->softwarePcmSample(),sample),"definition retained mutable metadata");
  MorphEndpoints endpoints;endpoints.b=makePcmCycle(WaveformShape::Triangle);endpoints.a[0]=-0.;
  auto morphed=Def::withMorphEndpoints(definition,endpoints);
  check(morphed!=definition&&morphed->softwarePcmSample().pcm==sample.pcm&&sameSoftwarePcmSample(morphed->softwarePcmSample(),sample),"Morph endpoint edit copied or altered source PCM");
  check(sameEndpoints(morphed->morphEndpoints(),endpoints),"Morph signed-zero endpoint lost");
  auto edited=sample;edited.baseFrequencyHz=440;edited.loop=false;
  auto changed=Def::fromSoftwarePcmSample(edited);
  check(changed->softwarePcmSample().pcm==sample.pcm,"root/playback edit did not share PCM");
  for(unsigned field=0;field<9;++field){auto invalid=sample;
    if(field==0)invalid.pcm.reset();if(field==1)invalid.baseFrequencyHz=0;
    if(field==2)invalid.baseFrequencyHz=std::numeric_limits<double>::quiet_NaN();
    if(field==3)invalid.baseFrequencyHz=1000001;if(field==4)invalid.volumeLeft=-1.01;
    if(field==5)invalid.volumeRight=std::numeric_limits<double>::infinity();
    if(field==6)invalid.inspectorPitch=16384;if(field==7)invalid.name=std::string(321,'x');
    if(field==8)invalid.name=std::string("x\0y",3);
    rejects([&]{validateSoftwarePcmSample(invalid);});rejects([&]{Def::fromSoftwarePcmSample(invalid);});
  }
  auto shortLoop=sample;shortLoop.pcm=SoftwarePcmBuffer::copyOf({0,1,2});rejects([&]{validateSoftwarePcmSample(shortLoop);});
  shortLoop.loop=false;validateSoftwarePcmSample(shortLoop);
  auto signedZero=sample;signedZero.volumeLeft=-0.;auto positiveZero=signedZero;positiveZero.volumeLeft=0.;
  check(!sameSoftwarePcmSample(signedZero,positiveZero),"signed zero voice volume comparison lost bits");
  SavedInstrumentState state;state.definition=morphed;
  allocations=largestAllocation=0;tracking=true;validateSavedInstrumentState(state);tracking=false;
  check(allocations==0,"validating software authoring state allocated encoded PCM");
}
SavedInstrumentState authored(){
  SavedInstrumentState state;auto sample=source();sample.volumeRight=-0.;
  MorphEndpoints endpoints;endpoints.a=makePcmCycle(WaveformShape::Triangle);endpoints.b[3]=-.1234567f;
  state.definition=Def::withMorphEndpoints(Def::fromSoftwarePcmSample(sample),endpoints);
  state.gainPercent=37;state.envelope={{0xa937,0xd7bb},true,false};
  state.reverb={ReverbPreset::Hall,true,true,.375,.625};state.noise={true,37};
  state.pmon={true,WaveformShape::Square,2.5,.375,1250};state.morph={true,.375};
  return state;
}
void codecAndMalformed(){
  const auto state=authored();const auto wire=encode(state);const auto n=word(wire,28);
  check(word(wire,8)==9&&word(wire,16)==4&&word(wire,20)==0&&word(wire,24)==0,"software schema9 header wrong");
  check(n==64+state.definition->softwarePcmSample().pcm->samples().size()*2+state.definition->softwarePcmSample().name.size(),"software payload length wrong");
  const auto restored=decode(wire);
  check(sameSoftwarePcmSample(restored.definition->softwarePcmSample(),state.definition->softwarePcmSample())&&
    sameEndpoints(restored.definition->morphEndpoints(),state.definition->morphEndpoints())&&encode(restored)==wire,"state9 lost PCM/metadata/26 settings");
  check(restored.definition->softwarePcmSample().pcm!=state.definition->softwarePcmSample().pcm,"decoded state borrowed host chunk storage");
  for(auto offset:{8u,12u,16u,20u,24u,28u,32u,36u,40u,44u,72u,80u,84u,88u,92u}){
    auto bad=wire;word(bad,offset,0xffffffffu);rejects([&]{decode(bad);});
  }
  for(auto offset:{16u,24u,32u}){
    auto bad=wire;for(unsigned i=0;i<8;++i)bad[32+offset+i]=static_cast<std::uint8_t>(0x7ff8000000000000ull>>(i*8));rejects([&]{decode(bad);});
  }
  for(unsigned version=1;version<=8;++version){auto bad=wire;word(bad,8,version);rejects([&]{decode(bad);});}
  {auto bad=wire;word(bad,8,10);rejects([&]{decode(bad);});}
  {auto bad=wire;bad[32+64+state.definition->softwarePcmSample().pcm->samples().size()*2]=0;rejects([&]{decode(bad);});}
  {auto bad=wire;word(bad,44+n,4);rejects([&]{decode(bad);});}
  {auto bad=wire;word(bad,72+n,2);rejects([&]{decode(bad);});}
  {auto bad=wire;word(bad,112+n,2);rejects([&]{decode(bad);});}
  {auto bad=wire;bad.push_back(0);rejects([&]{decode(bad);});}
  {auto bad=wire;bad.pop_back();rejects([&]{decode(bad);});}
  for(unsigned version=1;version<=8;++version){
    auto old=encodeState(Def::builtin(),25);const auto fixed=version==1?40:version==2?48:version<=4?72:version==5?80:version==6?112:kStateFixedBytes;
    old.resize(fixed);word(old,8,version);word(old,12,static_cast<std::uint32_t>(old.size()));
    const auto read=decode(old);check(read.definition->kind()==Def::Kind::Builtin&&read.gainPercent==25,"legacy builtin schema migration failed");
  }
  for(const auto& original:{Def::fromPcmCycle(makePcmCycle(WaveformShape::Triangle)),Def::fromAdpcmSample(extractAdpcmSource(*Def::builtin()))}){
    auto old=encodeState(original,99);word(old,8,8);const auto read=decode(old);
    check(read.definition->kind()==original->kind()&&read.gainPercent==99,"schema8 source migration failed");
  }
}
void capacity(){
  std::vector<std::int16_t> values(kMaxSoftwarePcmFrames,0);values.front()=-32768;values.back()=32767;
  auto buffer=SoftwarePcmBuffer::copyOf(values);values.clear();values.shrink_to_fit();
  SoftwarePcmSample sample;sample.pcm=buffer;sample.name=std::string(320,'x');sample.volumeLeft=-0.;
  auto def=Def::fromSoftwarePcmSample(sample);const auto wire=encodeState(def,100);
  check(wire.size()==kMaxStateBytes,"exact32MiB PCM capacity state length wrong");
  const auto restored=decode(wire);
  check(restored.definition->softwarePcmSample().pcm->samples().size()==kMaxSoftwarePcmFrames&&
    restored.definition->softwarePcmSample().pcm->samples().front()==-32768&&
    restored.definition->softwarePcmSample().pcm->samples().back()==32767,"maximum PCM payload was truncated");
  rejects([&]{decodeState({wire.data(),kMaxStateBytes+1});});
  auto over=std::vector<std::int16_t>(kMaxSoftwarePcmFrames+1);rejects([&]{SoftwarePcmBuffer::copyOf(over);});
}
void immediateAndAtomic(){
  auto state=authored();state.noise.enabled=false;state.pmon.enabled=false;state.morph.enabled=false;
  const auto wire=encode(state);const auto restored=decode(wire);
  SpuEngineExchange exchange;const auto original=Def::builtin();
  const auto pending=exchange.commitPrepared(prepareDefinition(*original),original);
  for(auto offset:{8u,32u,36u,40u,44u,72u,80u,84u,88u,92u}){
    auto bad=wire;word(bad,offset,0xffffffffu);
    rejects([&]{auto parsed=decode(bad);exchange.commitPreparedImport(prepareDefinition(*parsed.definition),parsed);});
    check(exchange.latestGeneration()==pending&&exchange.snapshotDefinition()==original,"malformed restore altered pending sound");
  }
  auto prepared=prepareDefinition(*restored.definition);
  allocations=largestAllocation=0;tracking=true;
  const auto committed=exchange.commitPreparedImport(std::move(prepared),restored);
  tracking=false;check(largestAllocation<4096,"import commit allocated full PCM state merely for validation");
  check(exchange.snapshotAuthoring().importedState->gainPercent==37,"pending import settings lost");
  check(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==committed,"software source was not adopted");
  auto independent=prepareDefinition(*state.definition);check(independent->prepare(48000),"software reference prepare failed");
  exchange.instrument().noteOn(60,127);independent->noteOn(60,127);
  std::vector<float> left(8192),right(8192),expectedLeft(8192),expectedRight(8192);
  exchange.instrument().render(left.data(),right.data(),8192);independent->render(expectedLeft.data(),expectedRight.data(),8192);
  check(left==expectedLeft&&right==expectedRight,"software first-note PCM differed after state restoration");
  float peak=0;for(float value:left)peak=std::max(peak,std::abs(value));check(peak>.005,"software restored source is silent");
}
}
int main(){try{buffersAndBase64();metadataAndSharing();codecAndMalformed();capacity();immediateAndAtomic();std::cout<<"PASS immutable shared Software PCM/PCM16LE/canonical base64; state9 26-field metadata/legacy1..8; exact32MiB boundary; zero-allocation validation/small import commit; malformed pending preservation and immediate Native PCM restoration\n";}catch(const std::exception& error){tracking=false;std::cerr<<error.what()<<'\n';return 1;}}
