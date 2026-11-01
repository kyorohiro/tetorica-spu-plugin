// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrument.h"
#include "WebInstrumentExport.h"
#include "WebInstrumentLoader.h"
#include "SpuEngineExchange.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

#ifndef WEB_FIXTURE_DIR
#define WEB_FIXTURE_DIR "tests/fixtures/web"
#endif
namespace {
using namespace tetorica;
using Json=nlohmann::json;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F action){bool threw=false;try{action();}catch(const std::exception&){threw=true;}check(threw,"invalid Web v2 accepted");}
std::string fixture(){std::ifstream input(std::filesystem::path(WEB_FIXTURE_DIR)/"v2-long-audio.spu.json",std::ios::binary);check(bool(input),"v2 fixture missing");return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
std::string withPcm(const Json& metadata,const SoftwarePcmBuffer& pcm){
  auto small=metadata;small["sample"]["pcm16Base64"]=nullptr;const auto body=small.dump();
  const std::string marker="\"pcm16Base64\":null";const auto at=body.find(marker);
  check(at!=std::string::npos,"test base64 marker missing");const auto begin=at+marker.size()-4;
  std::string output;output.append(body,0,begin);output+='"';appendSoftwarePcmBase64(pcm,output);
  output+='"';output.append(body,begin+4,std::string::npos);return output;
}
SoftwarePcmSample recording(std::size_t frames){
  std::vector<std::int16_t> values(frames);
  for(std::size_t i=0;i<frames;++i)values[i]=static_cast<std::int16_t>(std::lround(24000*std::sin(i*6.283185307179586/112)));
  SoftwarePcmSample sample;sample.pcm=SoftwarePcmBuffer::copyOf(values);sample.loop=true;
  sample.volumeLeft=-.375;sample.volumeRight=.625;sample.envelope={0x000f,0x3fc0};
  sample.inspectorPitch=1234;sample.reverb=false;sample.name="Software long 原音";return sample;
}
SavedInstrumentState stateFor(const SoftwarePcmSample& sample){
  SavedInstrumentState state;MorphEndpoints endpoints;endpoints.a=makePcmCycle(WaveformShape::Triangle);endpoints.b=makePcmCycle(WaveformShape::Square);
  state.definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromSoftwarePcmSample(sample),endpoints);
  state.gainPercent=37;state.envelope={{0x1234,0x1abc},true,false};
  state.reverb={ReverbPreset::Hall,false,false,.375,.625};state.noise={true,17};
  state.pmon={true,WaveformShape::Triangle,2.5,.375,1250};state.morph={true,.375};return state;
}
bool note(const ExportedWebInstrument& result,const char* needle){for(const auto& text:result.notes)if(text.find(needle)!=std::string::npos)return true;return false;}
void readerAndValidation(){
  const auto text=fixture();const auto expected=std::vector<std::int16_t>{0,32767,-32768,123};
  const auto imported=readWebInstrument(text);const auto& source=imported.state.definition->softwarePcmSample();
  check(imported.state.definition->kind()==InstrumentDefinition::Kind::SoftwarePcm&&source.pcm->samples()==expected,"actual Web v2 fixture PCM16LE values differ");
  check(source.baseFrequencyHz==220&&!source.loop&&source.volumeLeft==-.2&&source.volumeRight==.8&&
    source.envelope.adsr1==36682&&source.envelope.adsr2==59273&&source.inspectorPitch==4660&&source.reverb,"Web v2 metadata mapping differs");
  check(imported.state.reverb.preset==ReverbPreset::StudioSmall&&imported.state.reverb.enabled&&imported.state.reverb.useToneSend,"Web v2 shared Reverb mapping differs");
  const auto good=Json::parse(text);
  for(auto hz:{0.,.0000005,1000001.}){auto bad=good;bad["sample"]["baseFrequencyHz"]=hz;rejects([&]{readWebInstrument(bad.dump());});}
  for(auto hz:{.000001,1000000.}){auto edge=good;edge["sample"]["baseFrequencyHz"]=hz;check(readWebInstrument(edge.dump()).state.definition->softwarePcmSample().baseFrequencyHz==hz,"v2 root range endpoint rejected");}
  for(const auto value:{"","AA==","AAAA","AAB=","AA=A","====","AAA ","AA-_"}){auto bad=good;bad["sample"]["pcm16Base64"]=value;rejects([&]{readWebInstrument(bad.dump());});}
  for(std::size_t frames=1;frames<4;++frames){auto bad=good;bad["sample"]["loop"]=true;const auto pcm=SoftwarePcmBuffer::copyOf(std::vector<std::int16_t>(frames));rejects([&]{readWebInstrument(withPcm(bad,*pcm));});}
  {auto edge=good;edge["sample"]["loop"]=true;check(readWebInstrument(edge.dump()).state.definition->softwarePcmSample().loop,"4-frame v2 loop rejected");}
  {auto bad=good;bad["morph"]["enabled"]=true;rejects([&]{readWebInstrument(bad.dump());});}
  {auto bad=good;bad["waveform"]=PcmCycle{};rejects([&]{readWebInstrument(bad.dump());});}
  {auto bad=good;bad["noise"]["enabled"]=true;bad["pmon"]["enabled"]=true;rejects([&]{readWebInstrument(bad.dump());});}
  for(const auto& value:{Json(nullptr),Json::array({1,2}),Json(5)}){auto bad=good;bad["sample"]["pcm16Base64"]=value;rejects([&]{readWebInstrument(bad.dump());});}
  {auto bad=good;bad["pmon"]["pcm16Base64"]=std::string(2048,'A');rejects([&]{readWebInstrument(bad.dump());});}
  {auto bad=good;bad["sample"]["nested"]={{"pcm16Base64",std::string(2048,'A')}};rejects([&]{readWebInstrument(bad.dump());});}
  {auto bad=good;bad["name"]=std::string(1025,'a');rejects([&]{readWebInstrument(bad.dump());});}
  {auto duplicate=good.dump();const auto at=duplicate.find("\"pcm16Base64\":");check(at!=std::string::npos,"duplicate fixture marker missing");duplicate.insert(at,"\"pcm16Base64\":\"AAAAAAA=\",");rejects([&]{readWebInstrument(duplicate);});}
  {std::string oversized(kMaxWebInstrumentBytes+1,' ');rejects([&]{readWebInstrument(oversized);});}
  // Large-string permission is exact-path only; v1 keeps its original8MiB cap.
  {std::ifstream input(std::filesystem::path(WEB_FIXTURE_DIR)/"v1-defaults.spu.json",std::ios::binary);std::string old{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};old.insert(0,kMaxWebV1InstrumentBytes,' ');rejects([&]{readWebInstrument(old);});}
}
void exportMapping(){
  const auto sample=recording(1120);const auto state=stateFor(sample);
  const auto before=encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph);
  const auto output=writeWebInstrument(state,"  Software export 音  ");const auto imported=readWebInstrument(output.json);
  const auto& source=imported.state.definition->softwarePcmSample();
  check(sameSoftwarePcmSample(source,SoftwarePcmSample{sample.pcm,sample.baseFrequencyHz,sample.loop,
    sample.volumeLeft,sample.volumeRight,{state.envelope.custom.adsr1,static_cast<std::uint16_t>(state.envelope.custom.adsr2|0x2000)},true,sample.inspectorPitch,"Software export 音"}),"v2 export lost PCM or metadata projection");
  check(imported.state.gainPercent==100&&imported.state.noise.enabled&&!imported.state.pmon.enabled&&!imported.state.morph.enabled&&
    imported.state.pmon.ratio==2.5&&imported.state.morph.amount==.375,"v2 Gain/Noise/PMON/Morph normalization differs");
  check(imported.state.reverb.preset==ReverbPreset::Off&&imported.state.reverb.volumeLeft==.375&&imported.state.reverb.volumeRight==.625,"v2 disabled FX did not preserve returns");
  check(imported.state.definition->morphEndpoints().a==state.definition->morphEndpoints().a&&
    imported.state.definition->morphEndpoints().b==state.definition->morphEndpoints().b,"v2 masked Morph lost A/B originals");
  check(note(output,"Gain")&&note(output,"Software audio cannot use")&&note(output,"Custom ADSR")&&note(output,"All Notes")&&note(output,"Disabled Reverb"),"v2 export material notes missing");
  auto gainOnly=state;gainOnly.gainPercent=0;
  check(writeWebInstrument(gainOnly,"  Software export 音  ").json==output.json,"plugin Gain was baked into v2 recording");
  check(before==encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph),"v2 export mutated source");
  auto tone=state;tone.envelope.enabled=false;tone.reverb={ReverbPreset::Room,true,true,.25,.75};tone.noise.enabled=false;tone.pmon.enabled=false;tone.morph.enabled=false;
  const auto toneRead=readWebInstrument(writeWebInstrument(tone,"Tone send").json);
  check(!toneRead.state.definition->softwarePcmSample().reverb&&toneRead.state.definition->softwarePcmSample().envelope.adsr2==sample.envelope.adsr2&&toneRead.state.reverb.enabled,"v2 Tone send/source envelope mapping lost");
  const auto saved=encodeState(imported.state.definition,37,imported.state.envelope,imported.state.reverb,imported.state.noise,imported.state.pmon,imported.state.morph);
  const auto restored=decodeState({saved.data(),saved.size()});check(sameSoftwarePcmSample(source,restored.definition->softwarePcmSample()),"v2 recording not self-contained in state9");
}
void maximumAndLarge(){
  const auto metadata=Json::parse(fixture());
  const auto maximum=SoftwarePcmBuffer::copyOf(std::vector<std::int16_t>(kMaxSoftwarePcmFrames));
  auto text=withPcm(metadata,*maximum);check(text.size()>kMaxWebV1InstrumentBytes&&text.size()<kMaxWebInstrumentBytes,"maximum PCM v2 JSON bound incorrect");
  const auto imported=readWebInstrument(text);check(imported.state.definition->softwarePcmSample().pcm->samples().size()==kMaxSoftwarePcmFrames,"maximum v2 sample truncated");
  const auto payload=text.find("\"pcm16Base64\":\"");check(payload!=std::string::npos,"maximum payload marker missing");
  const auto begin=payload+15;const auto end=text.find('"',begin);check(end!=std::string::npos,"maximum payload end missing");
  auto over=text;over.insert(end,"AAAA");rejects([&]{readWebInstrument(over);});
  auto odd=text;odd[end-1]='A';rejects([&]{readWebInstrument(odd);});
}
WebInstrumentLoader::Status finished(WebInstrumentLoader& loader,WebInstrumentLoader::Ticket ticket){
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  for(;;){const auto status=loader.status();check(status.ticket==ticket,"stale Web v2 worker status escaped");if(status.state!=WebInstrumentLoader::State::Loading)return status;check(std::chrono::steady_clock::now()<end,"Web v2 worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
void worker(){
  WebInstrumentLoader loader;SpuEngineExchange exchange;const auto definition=InstrumentDefinition::builtin();
  const auto pending=exchange.commitPrepared(prepareDefinition(*definition),definition);const auto text=fixture();
  auto invalid=Json::parse(text);invalid["sample"]["pcm16Base64"]="AA==";
  const auto failure=loader.requestText(invalid.dump());
  check(finished(loader,failure).state==WebInstrumentLoader::State::Failed&&!loader.takeReady(failure),"invalid v2 worker produced engine");
  check(exchange.latestGeneration()==pending&&exchange.snapshotDefinition()==definition,"failed v2 changed pending sound");
  for(unsigned i=0;i<4;++i){
    const auto stale=loader.requestText(text);auto latest=Json::parse(text);latest["name"]="latest";
    const auto ticket=loader.requestText(latest.dump());check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready&&!loader.takeReady(stale),"latest v2 preparation did not win");
    auto ready=loader.takeReady(ticket);check(ready&&ready->imported.name=="latest"&&ready->engine->healthy()&&!loader.takeReady(ticket),"v2 engine/result not owned exactly once");
    const auto generation=exchange.commitPreparedImport(std::move(ready->engine),ready->imported.state);
    check(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==generation,"v2 prepared source not adopted");exchange.collectRetired();
  }
  auto ticket=loader.requestText(text);loader.cancel();check(loader.status().state==WebInstrumentLoader::State::Idle&&!loader.takeReady(ticket),"cancelled v2 prepared result escaped");
  ticket=loader.requestText(text);check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready,"v2 after cancel failed");
  loader.cancel();check(!loader.takeReady(ticket),"cancel failed to discard ready v2");
  for(unsigned i=0;i<3;++i){WebInstrumentLoader closing;closing.requestText(text);}
}
void writeFixtures(const std::filesystem::path& directory){
  std::filesystem::create_directories(directory);Json manifest{{"recordVersion",1},{"cases",Json::array()}};
  for(const auto& entry:{std::pair<const char*,std::size_t>{"v2-small",1120},{"v2-20MiB",20u*1024u*1024u/2}}){
    const auto sample=recording(entry.second);auto state=stateFor(sample);
    if(entry.second>1120){state.noise.enabled=false;state.pmon.enabled=false;state.morph.enabled=false;}
    const auto exported=writeWebInstrument(state,entry.first);
    const auto file=std::string(entry.first)+".spu.json",pcmFile=std::string(entry.first)+".pcm16le";
    {std::ofstream output(directory/file,std::ios::binary);output<<exported.json;check(bool(output),"software JSON fixture write failed");}
    {std::ofstream output(directory/pcmFile,std::ios::binary);for(const auto value:sample.pcm->samples()){const auto bits=static_cast<std::uint16_t>(value);output.put(static_cast<char>(bits&255));output.put(static_cast<char>(bits>>8));}check(bool(output),"software PCM fixture write failed");}
    auto smallState=state;smallState.definition=InstrumentDefinition::withMorphEndpoints(
      InstrumentDefinition::fromSoftwarePcmSample(recording(4)),state.definition->morphEndpoints());
    const auto small=writeWebInstrument(smallState,entry.first);auto metadata=Json::parse(small.json);metadata["sample"].erase("pcm16Base64");
    manifest["cases"].push_back({{"id",entry.first},{"file",file},{"version",2},{"sourceKind",4},{"frames",sample.pcm->samples().size()},
      {"pcm16File",pcmFile},{"metadata",std::move(metadata)},{"notes",exported.notes}});
  }
  std::ofstream output(directory/"software-manifest.json");output<<manifest.dump(2)<<'\n';check(bool(output),"software manifest write failed");
}
}
int main(int argc,char** argv){try{
  check(argc==1||(argc==3&&std::string(argv[1])=="--write-fixtures"),"usage: software_pcm_web_test [--write-fixtures DIR]");
  readerAndValidation();exportMapping();maximumAndLarge();worker();if(argc==3)writeFixtures(std::filesystem::u8path(argv[2]));
  std::cout<<"PASS actual Web v2 signed PCM fixture; exact32MiB/>8MiB paths, malformed/duplicates/path bounds, loss-aware metadata/Gain/Morph projection, state9, worker latest/cancel/atomicity\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
