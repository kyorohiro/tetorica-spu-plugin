// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrument.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef WEB_FIXTURE_DIR
#define WEB_FIXTURE_DIR "tests/fixtures/web"
#endif
namespace {
using namespace tetorica;
using Json=nlohmann::json;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string text(const std::string& file) {
  std::ifstream in(std::string(WEB_FIXTURE_DIR)+"/"+file,std::ios::binary);
  require(bool(in),"Missing Web fixture");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
template<class F> void rejects(F&& action,const char* contains=nullptr) {
  try{action();}catch(const std::exception& error){
    if(contains)require(std::string(error.what()).find(contains)!=std::string::npos,"Wrong rejection reason");return;
  }
  throw std::runtime_error("Invalid Web instrument was accepted");
}
ImportedWebInstrument read(const Json& value){return readWebInstrument(value.dump());}
PcmCycle cycle(const Json& value){PcmCycle result{};require(value.size()==112,"Wrong reference PCM length");for(unsigned i=0;i<112;++i)result[i]=value[i].get<float>();return result;}
void references() {
  const auto reference=Json::parse(text("reference.json"));
  for(const auto& entry:reference["cases"]) {
    const auto bytes=text(entry["file"].get<std::string>());
    if(entry["version"]==2){rejects([&]{readWebInstrument(bytes);},"version2 long software audio");continue;}
    const auto imported=readWebInstrument(bytes);const auto& state=imported.state;
    const auto source=Json::parse(bytes);const auto& expected=entry["expected"];
    require(imported.name==expected["name"],"Web name did not trim exactly");
    require(state.definition->kind()==InstrumentDefinition::Kind::AdpcmSample,"Web ADPCM was replaced by another source kind");
    const auto& sample=state.definition->rawAdpcmSample();
    require(sample.name==imported.name&&sample.adpcm==source["sample"]["adpcm"].get<std::vector<std::uint8_t>>(),"Web name/ADPCM original bytes changed");
    require(sample.baseFrequencyHz==expected["baseFrequencyHz"]&&sample.volumeLeft==expected["preset"]["volumeLeft"]&&
      sample.volumeRight==expected["preset"]["volumeRight"],"Web signed source volume or root frequency changed");
    require(sample.envelope.adsr1==expected["preset"]["adsr1"]&&sample.envelope.adsr2==expected["preset"]["adsr2"]&&
      sample.inspectorPitch==expected["preset"]["pitch"]&&sample.reverb==expected["preset"]["reverb"],"Web raw ADSR/pitch/send metadata changed");
    if(expected["loopStartSamples"].is_null())require(!sample.loopStartSamples,"One-shot acquired a loop");
    else require(sample.loopStartSamples&&*sample.loopStartSamples==expected["loopStartSamples"],"Web loop address changed");
    if(expected["waveform"].is_null())require(!sample.waveform,"ADPCM source acquired unrelated PCM metadata");
    else require(sample.waveform&&*sample.waveform==cycle(expected["waveform"]),"Canonical/legacy PCM original changed");
    require(state.gainPercent==100&&!state.envelope.enabled&&state.envelope.live,"Importer changed Gain or enabled Custom ADSR");
    require(state.reverb.volumeLeft==expected["reverb"]["volumeLeft"]&&state.reverb.volumeRight==expected["reverb"]["volumeRight"]&&
      state.reverb.useToneSend&&state.reverb.enabled==(expected["reverb"]["preset"]!="off"),"Web shared reverb mapping changed");
    require(state.noise.enabled==expected["noise"]["enabled"]&&state.noise.clock==expected["noise"]["clock"],"Web Noise changed");
    require(state.pmon.enabled==expected["pmon"]["enabled"]&&state.pmon.ratio==expected["pmon"]["ratio"]&&
      state.pmon.depth==expected["pmon"]["depth"]&&state.pmon.decayMs==expected["pmon"]["decayMs"],"Web PMON changed");
    require(state.morph.enabled==expected["morph"]["enabled"]&&state.morph.amount==expected["morph"]["amount"]&&
      state.definition->morphEndpoints().a==cycle(expected["morph"]["a"])&&state.definition->morphEndpoints().b==cycle(expected["morph"]["b"]),
      "Web Morph settings/default0.65 endpoints changed");
    const auto wire=encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph);
    const auto decoded=decodeState({wire.data(),wire.size()});
    require(encodeState(decoded.definition,decoded.gainPercent,decoded.envelope,decoded.reverb,decoded.noise,decoded.pmon,decoded.morph)==wire,
      "Imported Web authoring data did not survive DAW state");
  }
}
void malformed() {
  const auto good=Json::parse(text("v1-arbitrary.spu.json"));
  const auto check=[&](auto mutate){auto bad=good;mutate(bad);rejects([&]{read(bad);});};
  for(const char* key:{"schema","version","name","preset","sample","reverb"})check([&](Json& j){j.erase(key);});
  for(const char* key:{"volumeLeft","volumeRight","adsr1","adsr2","pitch","reverb"})check([&](Json& j){j["preset"].erase(key);});
  check([](Json& j){j["unexpected"]=0;});check([](Json& j){j["schema"]="other";});
  check([](Json& j){j["version"]=3;});check([](Json& j){j["version"]=1.5;});
  check([](Json& j){j["name"]=false;});check([](Json& j){j["name"]=" ";});
  for(const char* key:{"preset","sample","reverb","pmon","noise","morph"}) {
    check([&](Json& j){j[key]["unexpected"]=0;});
    check([&](Json& j){j[key]=nullptr;});
  }
  for(auto value:{-1.,65536.,.5})check([&](Json& j){j["preset"]["adsr1"]=value;});
  check([](Json& j){j["preset"]["adsr2"]=65536;});check([](Json& j){j["preset"]["pitch"]=16384;});
  check([](Json& j){j["preset"]["reverb"]=1;});
  for(auto value:{-1.01,1.01})check([&](Json& j){j["preset"]["volumeLeft"]=value;});
  for(auto value:{-1.,0.})check([&](Json& j){j["sample"]["baseFrequencyHz"]=value;});
  for(auto value:{-1.,256.,.5})check([&](Json& j){j["sample"]["adpcm"][2]=value;});
  check([](Json& j){j["sample"]["adpcm"]=Json::array();});
  check([](Json& j){j["sample"]["adpcm"].erase(0);});
  check([](Json& j){j["sample"]["adpcm"][0]=0x50;});check([](Json& j){j["sample"]["adpcm"][0]=13;});
  check([](Json& j){j["sample"]["adpcm"][1]=8;});check([](Json& j){j["sample"]["adpcm"][1]=1;});
  check([](Json& j){j["sample"]["adpcm"][33]=0;});
  check([](Json& j){j["sample"]["loopStartSamples"]=0;});
  check([](Json& j){j["pmon"]["enabled"]=true;j["noise"]["enabled"]=true;});
  check([](Json& j){j["noise"]["clock"]=64;});check([](Json& j){j["noise"]["clock"]=1.5;});
  check([](Json& j){j["pmon"]["ratio"]=.009;});check([](Json& j){j["pmon"]["depth"]=1.01;});
  check([](Json& j){j["pmon"]["decayMs"]=5001;});check([](Json& j){j["pmon"]["waveform"]="bad";});
  check([](Json& j){j["morph"]["enabled"]=true;});check([](Json& j){j["morph"]["amount"]=1.01;});
  check([](Json& j){j["morph"]["a"]=Json::array();});check([](Json& j){j["morph"]["b"]=Json::array();});
  check([](Json& j){j["reverb"]["preset"]="unknown";});check([](Json& j){j["reverb"]["volumeLeft"]=1.01;});
  for(const auto file:{"v1-waveform.spu.json","v1-waveform-legacy.spu.json","v1-morph.spu.json"}) {
    const auto wave=Json::parse(text(file));
    for(int change=0;change<6;++change) {
      auto bad=wave;
      if(change==0)bad["waveform"][17]=.9;
      if(change==1)bad["sample"]["adpcm"][63]=bad["sample"]["adpcm"][63].get<int>()^1;
      if(change==2)bad["sample"]["baseFrequencyHz"]=440;
      if(change==3)bad["waveform"].erase(0);
      if(change==4)bad["waveform"][0]=1.01;
      if(change==5)bad["sample"]["loopStartSamples"]=28;
      rejects([&]{read(bad);});
    }
  }
}
void syntaxAndBounds() {
  const auto good=Json::parse(text("v1-defaults.spu.json"));const auto wire=good.dump();
  for(const std::string& invalid:std::vector<std::string>{std::string{},"[]",wire+"{}",wire.substr(0,wire.size()-1),"/* comment */"+wire,
      std::string("{\"x\":NaN}"),std::string("{\"x\":1e309}"),std::string(20,'[')+"0"+std::string(20,']')})
    rejects([&]{readWebInstrument(invalid);});
  rejects([&]{readWebInstrument(wire+std::string("\0hidden",7));},"NUL");
  auto duplicate=wire;duplicate.insert(1,"\"name\":\"extra\",");rejects([&]{readWebInstrument(duplicate);},"duplicate");
  duplicate=wire;duplicate.insert(1,"\"\\u006eame\":\"extra\",");rejects([&]{readWebInstrument(duplicate);},"duplicate");
  duplicate=wire;const auto sample=duplicate.find("\"sample\":{");require(sample!=std::string::npos,"Fixture JSON shape changed");
  duplicate.insert(sample+10,"\"baseFrequencyHz\":220,");rejects([&]{readWebInstrument(duplicate);},"duplicate");
  rejects([&]{readWebInstrument(std::string(kMaxWebInstrumentBytes+1,' '));},"8MiB");
  std::string tooMany="[";for(int i=0;i<530001;++i){if(i)tooMany+=',';tooMany+='0';}tooMany+=']';
  rejects([&]{readWebInstrument(tooMany);},"too many values");
  auto max=good;max["sample"]["adpcm"]=std::vector<unsigned char>(520192,0);
  max["sample"]["adpcm"][520192-15]=1;
  require(read(max).state.definition->rawAdpcmSample().adpcm.size()==520192,"Maximum Web ADPCM rejected");
  max["sample"]["adpcm"].push_back(0);rejects([&]{read(max);});
  for(const double root:{1e-300,1e300}){auto edge=good;edge["sample"]["baseFrequencyHz"]=root;require(read(edge).state.definition->rawAdpcmSample().baseFrequencyHz==root,"Positive finite Web tuning rejected");}
  const std::array<const char*,8> presets{{"off","room","studio-small","studio-medium","studio-large","hall","space-echo","half-echo"}};
  for(unsigned i=0;i<presets.size();++i){auto j=good;j["reverb"]["preset"]=presets[i];const auto state=read(j).state;require(static_cast<unsigned>(state.reverb.preset)==i&&state.reverb.enabled==(i!=0),"Reverb enum mapping mismatch");}
}
void unicodeAndOwnership() {
  auto good=Json::parse(text("v1-defaults.spu.json"));
  good["name"]=std::string(80,'x');require(read(good).name.size()==80,"80-unit name rejected");
  good["name"]=std::string(81,'x');rejects([&]{read(good);});
  std::string emoji;for(int i=0;i<40;++i)emoji+="😀";
  good["name"]=emoji;require(read(good).name==emoji,"40 astral codepoints/80 UTF16 units rejected");
  good["name"]=emoji+"😀";rejects([&]{read(good);});
  good["name"]="　\t OK ﻿";require(read(good).name=="OK","ECMAScript whitespace trimming changed");
  good["name"]=std::string("a\0b",3);rejects([&]{read(good);});
  auto broken=Json::parse(text("v1-defaults.spu.json")).dump();const auto at=broken.find("Defaults");
  broken.replace(at,8,"\\ud800");rejects([&]{readWebInstrument(broken);});
  broken=Json::parse(text("v1-defaults.spu.json")).dump();broken.replace(broken.find("Defaults"),8,std::string("\xc0\xaf",2));
  rejects([&]{readWebInstrument(broken);});
  auto bytes=text("v1-morph.spu.json");auto imported=readWebInstrument(bytes);const auto original=imported.state.definition->rawAdpcmSample().adpcm;
  bytes.assign(bytes.size(),'x');require(imported.state.definition->rawAdpcmSample().adpcm==original,"Imported raw data aliases caller JSON storage");
}
}
int main() {
  try{references();malformed();syntaxAndBounds();unicodeAndOwnership();
    std::cout<<"PASS original Web v1 fixtures/current+legacy PCM/Morph0.65/rawmetadata/reverb/noise/pmon/state roundtrip; strict JSON/depth/nodes/duplicates/UTF16/bounds/ownership; v2 explicit rejection\n";
  }catch(const std::exception& error){std::cerr<<"FAIL Web import: "<<error.what()<<'\n';return 1;}
}
