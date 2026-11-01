// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "AudioFileLoader.h"
#include "AdpcmPlayback.h"
#include "SpuEngineExchange.h"
#include "WavImport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace tetorica;
using Loader=AudioFileLoader;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool sameMorphEndpoints(const MorphEndpoints& a,const MorphEndpoints& b){return a.a==b.a&&a.b==b.b;}
template<class F> void rejects(F action){bool threw=false;try{action();}catch(const std::exception&){threw=true;}check(threw,"invalid request accepted");}
void append16(std::vector<std::uint8_t>& bytes,unsigned value){bytes.push_back(value&255);bytes.push_back((value>>8)&255);}
void append32(std::vector<std::uint8_t>& bytes,std::uint32_t value){for(unsigned n=0;n<4;++n)bytes.push_back((value>>(8*n))&255);}
void text(std::vector<std::uint8_t>& bytes,const char* value){for(unsigned n=0;n<4;++n)bytes.push_back(value[n]);}
std::vector<std::uint8_t> wave(unsigned frames=560){
  std::vector<std::uint8_t> bytes;text(bytes,"RIFF");append32(bytes,36+frames*2);text(bytes,"WAVE");
  text(bytes,"fmt ");append32(bytes,16);append16(bytes,1);append16(bytes,1);append32(bytes,44100);append32(bytes,88200);append16(bytes,2);append16(bytes,16);
  text(bytes,"data");append32(bytes,frames*2);
  for(unsigned i=0;i<frames;++i)append16(bytes,static_cast<unsigned>(static_cast<std::int16_t>(std::lround(16000*std::sin(i*6.283185307179586/112)))));
  return bytes;
}
struct TempDirectory {
  std::filesystem::path path=std::filesystem::temp_directory_path()/std::filesystem::u8path("tetorica-wav-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  TempDirectory(){std::filesystem::create_directory(path);}
  ~TempDirectory(){std::error_code error;std::filesystem::remove_all(path,error);}
};
void write(const std::filesystem::path& path,const std::vector<std::uint8_t>& bytes){std::ofstream output(path,std::ios::binary);output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(output),"test WAV write failed");}
Loader::Status finished(Loader& loader,Loader::Ticket ticket){
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
  for(;;){const auto status=loader.status();check(status.ticket==ticket,"worker published stale ticket");if(status.state!=Loader::State::Loading)return status;check(std::chrono::steady_clock::now()<deadline,"WAV worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
Loader::Prepared ready(Loader& loader,Loader::Ticket ticket){
  const auto status=finished(loader,ticket);
  if(status.state!=Loader::State::Ready)throw std::runtime_error("preparation failed: "+status.error);
  auto result=loader.takeReady(ticket);check(result&&result->ticket==ticket&&result->definition&&result->engine&&result->engine->healthy(),"prepared ownership missing");
  check(!loader.takeReady(ticket)&&loader.status().state==Loader::State::Idle,"result consumed twice");
  return std::move(*result);
}
void filesAndState(const TempDirectory& temp){
  const auto bytes=wave();const auto path=temp.path/std::filesystem::u8path("\u3000日本語 WAV \u00a0.wav");write(path,bytes);
  MorphEndpoints endpoints;endpoints.a=makePcmCycle(WaveformShape::Square);endpoints.b=makePcmCycle(WaveformShape::Triangle);
  Loader loader;auto result=ready(loader,loader.requestFile(path.u8string(),endpoints));
  check(result.fileImport&&result.details.find("44100 Hz mono")!=std::string::npos&&result.details.find("C4 / one shot")!=std::string::npos,"WAV details missing");
  const auto& raw=result.definition->rawAdpcmSample();
  check(result.definition->kind()==InstrumentDefinition::Kind::AdpcmSample&&raw.name=="日本語 WAV","UTF8 basename was not trimmed or extension removed");
  check(raw.baseFrequencyHz==440.*std::pow(2.,(60-69)/12.)&&!raw.loopStartSamples,"WAV defaults not C4 one-shot");
  check(sameMorphEndpoints(result.definition->morphEndpoints(),endpoints),"WAV import lost global Morph endpoints");
  const auto expected=importWav({bytes.data(),bytes.size()},"日本語 WAV");
  check(sameRawAdpcmSample(raw,expected.sample),"file worker changed decoded sample metadata");
  const auto wire=encodeState(result.definition,37);
  const auto saved=decodeState({wire.data(),wire.size()});
  check(sameRawAdpcmSample(raw,saved.definition->rawAdpcmSample())&&sameMorphEndpoints(saved.definition->morphEndpoints(),endpoints),"WAV state8 not self-contained");
  auto reference=prepareDefinition(*saved.definition);
  check(reference->prepare(48000)&&result.engine->prepare(48000),"WAV prepare failed");
  reference->noteOn(60,127);result.engine->noteOn(60,127);
  std::vector<float> left(8192),right(8192),expectedLeft(8192),expectedRight(8192);
  reference->render(expectedLeft.data(),expectedRight.data(),8192);result.engine->render(left.data(),right.data(),8192);
  check(left==expectedLeft&&right==expectedRight,"embedded WAV first note differed after restore");
  float peak=0;for(float value:left)peak=std::max(peak,std::abs(value));check(peak>.005,"WAV Native restoration silent");
  const auto changed=withAdpcmPlayback(withAdpcmRoot(raw,57),true);
  const auto definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromAdpcmSample(changed),endpoints);
  auto metadata=ready(loader,loader.requestDefinition(definition));
  check(!metadata.fileImport&&metadata.definition==definition&&sameRawAdpcmSample(metadata.definition->rawAdpcmSample(),changed),"metadata worker lost definition or fields");
  check(sameMorphEndpoints(metadata.engine->morphEndpoints(),endpoints),"metadata engine did not prepare Morph endpoints");
}
void failureAndLatest(const TempDirectory& temp){
  const auto good=temp.path/"valid.wav";write(good,wave());
  const auto broken=temp.path/"truncated.wav";auto truncated=wave();truncated.pop_back();write(broken,truncated);
  const auto empty=temp.path/"empty.wav";write(empty,{});
  const auto oversized=temp.path/"oversized.wav";
  {std::ofstream output(oversized,std::ios::binary);output.seekp(static_cast<std::streamoff>(kMaxWavFileBytes));output.put(0);check(bool(output),"oversized fixture creation failed");}
  SpuEngineExchange exchange;const auto original=InstrumentDefinition::builtin();
  const auto pending=exchange.commitPrepared(prepareDefinition(*original),original);
  Loader loader;
  for(const auto& path:{broken,empty,oversized,temp.path/"missing.wav",temp.path}){
    const auto ticket=loader.requestFile(path.u8string());const auto status=finished(loader,ticket);
    check(status.state==Loader::State::Failed&&!status.error.empty()&&!loader.takeReady(ticket),"invalid WAV produced prepared engine");
    check(exchange.latestGeneration()==pending&&exchange.snapshotDefinition()==original,"failed WAV staging modified pending sound");
  }
  check(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==pending,"valid pending sound lost after WAV failure");
  auto ticket=loader.requestFile(good.u8string());
  rejects([&]{loader.requestFile("");});rejects([&]{loader.requestFile(std::string("x\0y",3));});rejects([&]{loader.requestDefinition(nullptr);});
  MorphEndpoints invalid;invalid.a[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{loader.requestFile(good.u8string(),invalid);});
  check(loader.status().ticket==ticket,"invalid caller request cancelled valid preparation");
  ready(loader,ticket);
  for(int iteration=0;iteration<12;++iteration){
    const auto stale=loader.requestFile(good.u8string());
    const auto latest=InstrumentDefinition::fromPcmCycle(makePcmCycle(iteration%2?WaveformShape::Square:WaveformShape::Triangle));
    ticket=loader.requestDefinition(latest);auto result=ready(loader,ticket);
    check(!loader.takeReady(stale)&&result.definition==latest&&!result.fileImport,"older worker completion overrode latest request");
  }
  ticket=loader.requestFile(good.u8string());loader.cancel();
  const auto cancelled=loader.status();check(cancelled.ticket>ticket&&cancelled.state==Loader::State::Idle&&!loader.takeReady(ticket),"cancel allowed a prepared result to escape");
  ticket=loader.requestDefinition(original);check(ticket>cancelled.ticket,"tickets did not remain monotonic");ready(loader,ticket);
  for(unsigned iteration=0;iteration<8;++iteration){Loader closing;closing.requestFile(good.u8string());}
}
void names(const TempDirectory& temp){
  Loader loader;const auto bytes=wave(28);
  for(const auto& value:{std::string(79,'a')+"😀",std::string(78,'b')+"😀"}){
    const auto path=temp.path/std::filesystem::u8path(value+".wav");write(path,bytes);
    auto result=ready(loader,loader.requestFile(path.u8string()));
    const auto expected=value[0]=='a'?std::string(79,'a'):value;
    check(result.definition->rawAdpcmSample().name==expected,"80 UTF16 unit filename bound split or over-counted a Unicode point");
  }
  const auto spaces=temp.path/"   .wav";write(spaces,bytes);
  auto fallback=ready(loader,loader.requestFile(spaces.u8string()));
  check(fallback.definition->rawAdpcmSample().name=="Imported WAV","empty trimmed stem has no fallback");
#ifndef _WIN32
  const auto invalid=temp.path/std::filesystem::path(std::string("bad-\xc0\x80.wav",10));
  // Some filesystems reject invalid UTF-8 names at creation (including the
  // macOS test volume). Linux filesystems may permit them; either path must
  // fail staging, never produce an invalid instrument name.
  {std::ofstream output(invalid,std::ios::binary);if(output)output.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));}
  const auto ticket=loader.requestFile(invalid.u8string());
  check(finished(loader,ticket).state==Loader::State::Failed,"invalid UTF8 filename accepted");
#endif
}
}
int main(){try{TempDirectory temp;filesAndState(temp);failureAndLatest(temp);names(temp);std::cout<<"PASS WAV worker bounded files, Unicode/UTF16 names, metadata/native+state8 restoration, Morph preservation, latest/cancel/one-take and close/join; exchange remains unchanged on failure\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
