// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrumentExport.h"
#include "WebInstrument.h"
#include "SpuExportSource.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef WEB_FIXTURE_DIR
#define WEB_FIXTURE_DIR "tests/fixtures/web"
#endif
#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
namespace {
using namespace tetorica;using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string fixture(const char* name) {
  std::ifstream in(std::string(WEB_FIXTURE_DIR)+"/"+name,std::ios::binary);
  require(bool(in),"Missing Web fixture");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
std::vector<std::uint8_t> vab() {
  std::ifstream in(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
  require(bool(in),"Missing original VAB fixture");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
bool note(const ExportedWebInstrument& value,const char* match) {
  for(const auto& message:value.notes)if(message.find(match)!=std::string::npos)return true;return false;
}
template<class F>void rejects(F&& action,const char* contains=nullptr) {
  try{action();}catch(const std::exception& error){
    if(contains)require(std::string(error.what()).find(contains)!=std::string::npos,"Wrong export rejection reason");return;
  }
  throw std::runtime_error("Invalid/unrepresentable export was accepted");
}
std::vector<std::uint8_t> stateBytes(const SavedInstrumentState& state) {
  return encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph);
}
struct Case {std::string id;SavedInstrumentState state;};
std::vector<Case> cases() {
  SavedInstrumentState builtin;builtin.definition=InstrumentDefinition::builtin();
  auto pcm=builtin;pcm.definition=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  auto selected=builtin;selected.definition=InstrumentDefinition::fromVab(vab(),3,1);
  auto raw=readWebInstrument(fixture("v1-arbitrary.spu.json")).state;
  auto legacy=readWebInstrument(fixture("v1-waveform-legacy.spu.json")).state;
  auto morph=readWebInstrument(fixture("v1-morph.spu.json")).state;
  std::vector<Case> result{{"builtin",builtin},{"pcm",pcm},{"vab",selected},{"raw",raw},{"legacy-pcm",legacy},{"imported-morph",morph}};
  for(const auto& base:std::vector<Case>{{"builtin",builtin},{"pcm",pcm},{"vab",selected},{"raw",raw}}) {
    auto state=base.state;state.morph={true,.375};
    result.push_back({base.id+"-morph",state});
  }
  auto custom=raw;custom.envelope={{0x1234,0x1abc},true,false};result.push_back({"custom-adsr",custom});
  auto inactive=raw;inactive.envelope={{0x1234,0x1abc},false,false};result.push_back({"inactive-adsr",inactive});
  auto masked=raw;masked.noise={true,8};masked.pmon={true,WaveformShape::Triangle,2.5,.375,1250};masked.morph={true,.375};
  result.push_back({"noise-masked-pmon-morph",masked});
  auto disabled=raw;disabled.reverb={ReverbPreset::Hall,false,true,.375,.625};result.push_back({"disabled-reverb",disabled});
  auto send=raw;auto noSend=send.definition->rawAdpcmSample();noSend.reverb=false;send.definition=InstrumentDefinition::fromAdpcmSample(noSend);
  send.reverb={ReverbPreset::Room,true,false,.375,.625};result.push_back({"all-notes-send",send});
  send.reverb.useToneSend=true;result.push_back({"tone-send-off",send});
  auto enabledOff=raw;enabledOff.reverb={ReverbPreset::Off,true,false,.375,.625};result.push_back({"enabled-off-preset",enabledOff});
  auto gain=raw;gain.gainPercent=0;result.push_back({"gain-zero",gain});gain.gainPercent=37;result.push_back({"gain37",gain});
  auto zero=raw;auto zeroSample=zero.definition->rawAdpcmSample();zeroSample.volumeLeft=-0.;zero.definition=InstrumentDefinition::fromAdpcmSample(zeroSample);
  result.push_back({"negative-zero-volume",zero});
  return result;
}
Json sourceJson(const RawAdpcmSample& sample) {
  Json value{{"adpcm",sample.adpcm},{"baseFrequencyHz",sample.baseFrequencyHz},
    {"volumeLeft",sample.volumeLeft},{"volumeRight",sample.volumeRight},{"adsr1",sample.envelope.adsr1},
    {"adsr2",sample.envelope.adsr2},{"pitch",sample.inspectorPitch},{"reverb",sample.reverb},
    {"waveform",sample.waveform?Json(*sample.waveform):Json(nullptr)}};
  value["loopStartSamples"]=sample.loopStartSamples?Json(*sample.loopStartSamples):Json(nullptr);return value;
}
Json settingsJson(const SavedInstrumentState& state) {
  return {{"gainPercent",state.gainPercent},
    {"envelope",{{"enabled",state.envelope.enabled},{"live",state.envelope.live},{"adsr1",state.envelope.custom.adsr1},{"adsr2",state.envelope.custom.adsr2}}},
    {"reverb",{{"preset",static_cast<unsigned>(state.reverb.preset)},{"enabled",state.reverb.enabled},
      {"useToneSend",state.reverb.useToneSend},{"volumeLeft",state.reverb.volumeLeft},{"volumeRight",state.reverb.volumeRight}}},
    {"noise",{{"enabled",state.noise.enabled},{"clock",state.noise.clock}}},
    {"pmon",{{"enabled",state.pmon.enabled},{"waveform",static_cast<unsigned>(state.pmon.waveform)},
      {"ratio",state.pmon.ratio},{"depth",state.pmon.depth},{"decayMs",state.pmon.decayMs}}},
    {"morph",{{"enabled",state.morph.enabled},{"amount",state.morph.amount},
      {"a",state.definition->morphEndpoints().a},{"b",state.definition->morphEndpoints().b}}}};
}
void matrix(const std::vector<Case>& values) {
  for(const auto& entry:values) {
    const auto& state=entry.state;const auto before=stateBytes(state);
    const auto raw=extractAdpcmSource(*state.definition);const auto value=writeWebInstrument(state,"  Export 音色  ");
    require(value.json.size()<=kMaxWebInstrumentBytes&&stateBytes(state)==before,"Export mutated authoring state or exceeded bounds");
    require(note(value,"Gain")&&note(value,"Host bypass"),"Material host/Gain differences were not disclosed");
    const auto parsed=readWebInstrument(value.json);const auto projection=Json::parse(value.json);
    const auto& source=parsed.state.definition->rawAdpcmSample();
    require(parsed.name=="Export 音色"&&source.volumeLeft==raw.volumeLeft&&source.volumeRight==raw.volumeRight&&parsed.state.gainPercent==100,
      "Name trimming or portable source volume/Gain policy changed");
    const auto expected1=state.envelope.enabled?state.envelope.custom.adsr1:raw.envelope.adsr1;
    const auto expected2=state.envelope.enabled?static_cast<std::uint16_t>(state.envelope.custom.adsr2|(raw.envelope.adsr2&0x2000)):raw.envelope.adsr2;
    require(source.envelope.adsr1==expected1&&source.envelope.adsr2==expected2,"Effective ADSR or reserved source bit was not baked correctly");
    require(source.inspectorPitch==raw.inspectorPitch&&source.reverb==(state.reverb.useToneSend?raw.reverb:true),"Inspector pitch/effective voice send changed");
    require(parsed.state.noise.enabled==state.noise.enabled&&parsed.state.pmon.enabled==(state.pmon.enabled&&!state.noise.enabled)&&
      parsed.state.pmon.ratio==state.pmon.ratio&&parsed.state.pmon.depth==state.pmon.depth&&parsed.state.pmon.decayMs==state.pmon.decayMs,
      "Noise priority lost retained PMON authoring settings");
    const bool effectiveMorph=state.morph.enabled&&!state.noise.enabled;
    require(parsed.state.morph.enabled==effectiveMorph&&parsed.state.morph.amount==state.morph.amount&&
      parsed.state.definition->morphEndpoints().a==state.definition->morphEndpoints().a&&
      parsed.state.definition->morphEndpoints().b==state.definition->morphEndpoints().b,"Morph priority lost amount/endpoints");
    if(effectiveMorph) {
      const auto cycle=blendMorph(state.definition->morphEndpoints(),state.morph.amount);const auto bytes=encodePcmCycle(cycle);
      require(source.waveform&&*source.waveform==cycle&&source.adpcm==std::vector<std::uint8_t>(bytes.begin(),bytes.end())&&
        source.baseFrequencyHz==44100./112&&source.loopStartSamples==std::optional<std::uint32_t>(0),"Active Morph was not baked as a matching canonical cycle");
      require(note(value,"original base sample")&&note(value,"16-bit"),"Active Morph losses were not disclosed");
    } else require(source.adpcm==raw.adpcm&&source.baseFrequencyHz==raw.baseFrequencyHz&&source.loopStartSamples==raw.loopStartSamples&&
      source.waveform==raw.waveform,"Morph-off export changed original sample bytes/tuning/PCM metadata");
    require(parsed.state.reverb.preset==(state.reverb.enabled?state.reverb.preset:ReverbPreset::Off)&&
      parsed.state.reverb.volumeLeft==state.reverb.volumeLeft&&parsed.state.reverb.volumeRight==state.reverb.volumeRight,"Reverb Off/return projection changed");
    if(entry.id=="legacy-pcm")require(note(value,"legacy ADPCM"),"Web legacy re-encoding loss was not disclosed");
    if(state.noise.enabled&&state.pmon.enabled)require(note(value,"masked PMON"),"Masked PMON flag loss was not disclosed");
    if(state.noise.enabled&&state.morph.enabled)require(note(value,"masked Morph"),"Masked Morph flag loss was not disclosed");
    if(!state.reverb.enabled&&state.reverb.preset!=ReverbPreset::Off)require(note(value,"dormant selected preset"),"Disabled Reverb preset loss was not disclosed");
    if(entry.id=="negative-zero-volume")require(std::signbit(projection["preset"]["volumeLeft"].get<double>()),"Signed zero source volume lost");
  }
  auto a=values.at(3).state,b=a;b.gainPercent=37;
  require(writeWebInstrument(a,"Gain").json==writeWebInstrument(b,"Gain").json,"Gain was silently baked despite authored-volume policy");
}
SavedInstrumentState sized(std::size_t bytes,bool pmon) {
  RawAdpcmSample source;source.adpcm.resize(bytes);source.adpcm[bytes-15]=1;
  SavedInstrumentState state;state.definition=InstrumentDefinition::fromAdpcmSample(source);state.pmon.enabled=pmon;return state;
}
void boundsAndFailures(const SavedInstrumentState& good) {
  for(bool pmon:{false,true}) {
    const auto available=524288-4096-24*64-(pmon?64:0);
    require(!writeWebInstrument(sized(available,pmon),"Fresh Web boundary").json.empty(),"Fresh Web RAM boundary rejected");
    rejects([&]{writeWebInstrument(sized(available+16,pmon),"Too large");},"fresh Web");
  }
  auto empty=good;empty.definition.reset();rejects([&]{writeWebInstrument(empty,"Missing");});
  for(double invalid:{-.01,100.01,std::numeric_limits<double>::quiet_NaN()}) {
    auto bad=good;bad.gainPercent=invalid;rejects([&]{writeWebInstrument(bad,"Invalid");});
  }
  auto bad=good;bad.noise.enabled=true;bad.pmon.waveform=static_cast<WaveformShape>(4);rejects([&]{writeWebInstrument(bad,"Masked invalid PMON");});
  bad=good;bad.noise.enabled=true;bad.morph.amount=std::numeric_limits<double>::quiet_NaN();rejects([&]{writeWebInstrument(bad,"Masked invalid Morph");});
  for(const auto& name:std::vector<std::string>{"", " ",std::string(81,'a'),std::string("a\0b",3),std::string("\xc0\xaf",2)})
    rejects([&]{writeWebInstrument(good,name);});
  const auto original=stateBytes(good);rejects([&]{writeWebInstrument(good," ");});require(stateBytes(good)==original,"Failed export mutated source");
}
void writeFixtures(const std::vector<Case>& values,const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);Json manifest{{"description","Original source matrix projected by the pure C++ Web v1 writer. Source metadata and plugin controls are included for independent Web projection checks."},{"cases",Json::array()}};
  for(const auto& entry:values) {
    const auto value=writeWebInstrument(entry.state,"Export "+entry.id);const auto file=entry.id+".spu.json";
    std::ofstream out(directory/file,std::ios::binary);require(bool(out),"Cannot create exported fixture");out<<value.json;require(bool(out),"Cannot write exported fixture");
    manifest["cases"].push_back({{"id",entry.id},{"file",file},{"sourceKind",static_cast<unsigned>(entry.state.definition->kind())},
      {"notes",value.notes},{"originalSource",sourceJson(extractAdpcmSource(*entry.state.definition))},
      {"plugin",settingsJson(entry.state)},{"projected",Json::parse(value.json)}});
  }
  std::ofstream out(directory/"manifest.json",std::ios::binary);out<<manifest.dump(2)<<'\n';require(bool(out),"Cannot write projection manifest");
}
}
int main(int argc,char** argv) {
  try{
    if(argc!=1&&(argc!=3||std::string(argv[1])!="--write-fixtures"))throw std::invalid_argument("Usage: web_instrument_export_test [--write-fixtures OUTPUTDIR]");
    const auto values=cases();matrix(values);boundsAndFailures(values.at(3).state);
    if(argc==3)writeFixtures(values,std::filesystem::u8path(argv[2]));
    std::cout<<"PASS Web v1 export all4sources/originalRaw+legacy/effectiveADSR+reserved/send/reverbOff/Noisepriority/Morphcanonical/Gainexcluded/materialnotes/selfread/freshWebRAM/invalidunchanged; matrix="<<values.size()<<'\n';
  }catch(const std::exception& error){std::cerr<<"FAIL Web export: "<<error.what()<<'\n';return 1;}
}
