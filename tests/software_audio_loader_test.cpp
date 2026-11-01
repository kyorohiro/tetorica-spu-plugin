// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "AudioFileLoader.h"
#include "WavImport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace tetorica;
using Bytes=std::vector<std::uint8_t>;
namespace fs=std::filesystem;
constexpr std::size_t adpcmFrames=vab::kMaxSampleBytes/16*28;
constexpr std::size_t largeFrames=(20u*1024u*1024u-44)/2;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"Invalid software WAV input accepted");}
void word(Bytes& out,std::size_t at,std::uint32_t value,unsigned count=4){for(unsigned n=0;n<count;++n)out.at(at+n)=static_cast<std::uint8_t>(value>>(8*n));}
std::int16_t original(std::size_t frame){return static_cast<std::int16_t>(static_cast<int>((frame*97)%60001)-30000);}
Bytes wav(std::size_t frames,unsigned rate=44100,bool floating=false) {
  const unsigned width=floating?4:2;Bytes bytes(44+frames*width);
  for(auto field:std::vector<std::pair<std::size_t,const char*>>{{0,"RIFF"},{8,"WAVE"},{12,"fmt "},{36,"data"}})
    std::memcpy(bytes.data()+field.first,field.second,4);
  word(bytes,4,static_cast<std::uint32_t>(bytes.size()-8));word(bytes,16,16);word(bytes,20,floating?3:1,2);
  word(bytes,22,1,2);word(bytes,24,rate);word(bytes,28,rate*width);word(bytes,32,width,2);word(bytes,34,width*8,2);
  word(bytes,40,static_cast<std::uint32_t>(frames*width));
  for(std::size_t frame=0;frame<frames;++frame) {
    if(floating){const float value=original(frame)/32768.f;std::uint32_t bits;std::memcpy(&bits,&value,4);word(bytes,44+frame*4,bits);}
    else word(bytes,44+frame*2,static_cast<std::uint16_t>(original(frame)),2);
  }
  return bytes;
}
WavAudioImportResult convert(const Bytes& bytes,WavImportOptions options={}) {
  return importWavAudio({bytes.data(),bytes.size()},"Original Software audio 音",options);
}
void write(const fs::path& path,const Bytes& bytes){std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));check(bool(file),"Cannot write owned synthetic WAV");}
struct Directory {
  fs::path path=fs::absolute("software-audio-loader-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  Directory(){check(fs::create_directory(path),"Cannot create test directory");}
  ~Directory(){std::error_code ignored;fs::remove_all(path,ignored);}
};
AudioFileLoader::Status finished(AudioFileLoader& loader,AudioFileLoader::Ticket ticket) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  for(;;){const auto status=loader.status();check(status.ticket==ticket,"Stale software job replaced latest status");
    if(status.state!=AudioFileLoader::State::Loading)return status;
    check(std::chrono::steady_clock::now()<end,"Software audio worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
AudioFileLoader::Prepared ready(AudioFileLoader& loader,AudioFileLoader::Ticket ticket) {
  const auto status=finished(loader,ticket);if(status.state!=AudioFileLoader::State::Ready)throw std::runtime_error(status.error);
  auto result=loader.takeReady(ticket);check(result&&result->engine&&result->engine->healthy(),"Software worker lost prepared ownership");
  check(!loader.takeReady(ticket),"Software prepared result consumed twice");return std::move(*result);
}
void automaticAndQuantization() {
  // Cheap silence reaches the complete physical-RAM boundary without making
  // this a performance test of the existing ADPCM search.
  auto edge=wav(adpcmFrames);std::fill(edge.begin()+44,edge.end(),0);
  const auto old=importWav({edge.data(),edge.size()},"Original Software audio 音");
  const auto exact=convert(edge);check(!exact.softwareAudio()&&
    sameRawAdpcmSample(std::get<RawAdpcmSample>(exact.sample),old.sample),"ADPCM boundary changed original bytes");
  const auto beyond=convert(wav(adpcmFrames+1),{57,true});
  check(beyond.softwareAudio()&&beyond.paddedFrames==0&&beyond.resampledFrames==adpcmFrames+1,
    "Physical-RAM overflow did not select exact-length Software PCM");
  const auto& software=std::get<SoftwarePcmSample>(beyond.sample);
  check(software.loop&&software.baseFrequencyHz==220&&software.pcm->samples().size()==adpcmFrames+1,
    "Software WAV lost authored root/loop/frames");
  for(std::size_t n=0;n<software.pcm->samples().size();++n)
    check(software.pcm->samples()[n]==original(n),"PCM16 WAV was quantized a second time");
  auto ties=wav(adpcmFrames+1,44100,true);
  for(unsigned n=0;n<4;++n){const float value=(n==0?-.5f:n==1?.5f:n==2?-1.5f:1.5f)/32768.f;
    std::uint32_t bits;std::memcpy(&bits,&value,4);word(ties,44+n*4,bits);}
  const auto rounded=convert(ties);const auto& values=std::get<SoftwarePcmSample>(rounded.sample).pcm->samples();
  check(values[0]==0&&values[1]==1&&values[2]==-1&&values[3]==2,"Software PCM differs from Web Math.round at signed half values");
  // At 8 kHz the input still fits 32 MiB, but its decoded output cannot. The
  // output bound must reject before allocating/resampling the much larger PCM.
  const auto tooLong=wav(kMaxSoftwarePcmFrames*8000/44100+1,8000);
  rejects([&]{convert(tooLong);});
  rejects([&]{convert(edge,{-1,false});});rejects([&]{convert(edge,{128,false});});
  auto malformed=ties;word(malformed,44,0x7fc00000);rejects([&]{convert(malformed);});
}
void largeFileAndState(const Directory& directory) {
  const auto path=directory.path/fs::u8path("20MiB ソフト音声.wav");
  {const auto bytes=wav(largeFrames);check(bytes.size()==20u*1024u*1024u,"20 MiB fixture size wrong");write(path,bytes);}
  MorphEndpoints endpoints{makePcmCycle(WaveformShape::Triangle),makePcmCycle(WaveformShape::Square)};
  AudioFileLoader loader;auto prepared=ready(loader,loader.requestFile(path.u8string(),endpoints));
  check(prepared.fileImport&&prepared.definition->kind()==InstrumentDefinition::Kind::SoftwarePcm&&
    prepared.details.find("Software audio")!=std::string::npos&&prepared.details.find("seconds")!=std::string::npos,
    "Large WAV worker did not identify Software audio and duration");
  const auto& source=prepared.definition->softwarePcmSample();
  check(source.pcm&&source.pcm->samples().size()==largeFrames&&!source.loop&&source.name=="20MiB ソフト音声"&&
    source.baseFrequencyHz==440*std::pow(2.,-9./12),"20 MiB WAV metadata/owned frame count changed");
  for(std::size_t n=0;n<largeFrames;++n)check(source.pcm->samples()[n]==original(n),"20 MiB source PCM changed");
  check(prepared.definition->morphEndpoints().a==endpoints.a&&prepared.definition->morphEndpoints().b==endpoints.b,
    "Software import lost dormant Morph endpoints");
  check(fs::remove(path),"Could not remove own WAV before embedded restore");
  const auto wire=encodeState(prepared.definition,37,{{0xa937,0xd7bb},false,false},
    {ReverbPreset::Room,false,true,.375,.625},{false,37},{false,WaveformShape::Saw,2.5,.375,1250},{false,.375});
  check(wire.size()>20u*1024u*1024u&&wire.size()<=kMaxStateBytes,"Large software project payload size/cap wrong");
  const auto restored=decodeState({wire.data(),wire.size()});
  check(restored.definition->kind()==InstrumentDefinition::Kind::SoftwarePcm&&restored.gainPercent==37&&
    restored.definition->softwarePcmSample().pcm->samples()==source.pcm->samples()&&
    encodeState(restored.definition,restored.gainPercent,restored.envelope,restored.reverb,restored.noise,restored.pmon,restored.morph)==wire,
    "20 MiB source or complete authored tuple did not survive embedded state");
  auto reference=prepareDefinition(*restored.definition);
  check(prepared.engine->prepare(48000)&&reference->prepare(48000),"Software engines could not prepare host rate");
  prepared.engine->noteOn(60,127);reference->noteOn(60,127);
  std::vector<float> left(4096),right(4096),expectedLeft(4096),expectedRight(4096);
  prepared.engine->render(left.data(),right.data(),4096);reference->render(expectedLeft.data(),expectedRight.data(),4096);
  check(left==expectedLeft&&right==expectedRight&&std::any_of(left.begin(),left.end(),[](float value){return std::abs(value)>1e-4f;}),
    "Software PCM restore without WAV changed first-note audio");
  auto edited=source;edited.loop=true;edited.baseFrequencyHz=220;
  const auto definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromSoftwarePcmSample(edited),endpoints);
  check(definition->softwarePcmSample().pcm==source.pcm,"Metadata edit cloned immutable large PCM");
  auto metadata=ready(loader,loader.requestDefinition(definition));
  check(!metadata.fileImport&&metadata.definition==definition,"Worker lost prepared Software root/loop edit");
}
void latestAndFailure(const Directory& directory) {
  const auto path=directory.path/"long.wav";write(path,wav(adpcmFrames+1));
  AudioFileLoader loader;const auto old=loader.requestFile(path.u8string());
  const auto builtin=InstrumentDefinition::builtin();auto latest=ready(loader,loader.requestDefinition(builtin));
  check(latest.definition==builtin&&!loader.takeReady(old),"Old software file work replaced a newer source request");
  const auto ticket=loader.requestFile(path.u8string());loader.cancel();
  check(loader.status().ticket>ticket&&!loader.takeReady(ticket),"Cancel published stale software preparation");
  const auto missing=loader.requestFile((directory.path/"missing.wav").u8string());
  check(finished(loader,missing).state==AudioFileLoader::State::Failed,"Missing software file produced an engine");
  auto invalid=wav(adpcmFrames+1);invalid.pop_back();write(path,invalid);
  check(finished(loader,loader.requestFile(path.u8string())).state==AudioFileLoader::State::Failed,
    "Invalid long WAV produced a prepared engine");
}
void fixtures(const fs::path& directory) {
  fs::create_directories(directory);write(directory/"20MiB-pcm16-mono.wav",wav(largeFrames));
  write(directory/"software-threshold.wav",wav(adpcmFrames+1));
  std::ofstream file(directory/"manifest.json");
  file<<"{\n  \"description\":\"Original deterministic synthetic fixtures generated at runtime; large audio is not committed.\",\n  \"cases\":[\n"
      <<"    {\"wav\":\"20MiB-pcm16-mono.wav\",\"bytes\":20971520,\"frames\":"<<largeFrames<<",\"sampleRate\":44100,\"rootMidiNote\":60,\"loop\":false},\n"
      <<"    {\"wav\":\"software-threshold.wav\",\"frames\":"<<adpcmFrames+1<<",\"sampleRate\":44100,\"rootMidiNote\":60,\"loop\":false}\n  ]\n}\n";
  check(bool(file),"Cannot write generated large-fixture manifest");
}
}
int main(int argc,char** argv){try{automaticAndQuantization();Directory directory;largeFileAndState(directory);latestAndFailure(directory);
  if(argc==3&&std::string(argv[1])=="--write-fixtures")fixtures(fs::u8path(argv[2]));else check(argc==1,"Unexpected test arguments");
  std::cout<<"PASS Software audio WAV automaticThreshold=true oldAdpcmExact=true WebRound=true outputBound=true 20MiB=true exactOwnedPcm=true embeddedSourceWithoutWav=true full26State=true firstRestoredPcm=true sharedMetadataEdit=true latestCancelFailure=true\n";
}catch(const std::exception& error){std::cerr<<"FAIL Software audio loader: "<<error.what()<<'\n';return 1;}}
