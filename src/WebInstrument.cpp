// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Port of docs/synth-spu/spu_synth_instrument.js, *_morph.js, *_pmon.js and
// *_noise.js in hello_ymfm. Uses the pinned iPlug2 nlohmann JSON header (MIT).
#include "WebInstrument.h"
#include "SpuAdpcm.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace tetorica {
namespace {
using Json=nlohmann::json;
[[noreturn]] void fail(const std::string& reason) {throw std::invalid_argument("Web instrument: "+reason);}
const Json& member(const Json& value,const char* key) {
  const auto at=value.find(key);if(at==value.end())fail(std::string("missing ")+key);return *at;
}
void keys(const Json& value,std::initializer_list<const char*> allowed,const char* label) {
  if(!value.is_object())fail(std::string(label)+" must be an object");
  for(auto at=value.begin();at!=value.end();++at)
    if(std::none_of(allowed.begin(),allowed.end(),[&](const char* key){return at.key()==key;}))
      fail(std::string("unknown ")+label+" field: "+at.key());
}
double number(const Json& value,double minimum,double maximum,const char* label,bool integral=false) {
  if(!value.is_number())fail(std::string(label)+" must be a number");
  const double result=value.get<double>();
  if(!std::isfinite(result)||result<minimum||result>maximum||(integral&&std::floor(result)!=result))
    fail(std::string(label)+" is outside its valid range");
  return result;
}
bool boolean(const Json& value,const char* label) {
  if(!value.is_boolean())fail(std::string(label)+" must be boolean");return value.get<bool>();
}
bool whitespace(std::uint32_t value) {
  return (value>=9&&value<=13)||value==32||value==0xa0||value==0x1680||
    (value>=0x2000&&value<=0x200a)||value==0x2028||value==0x2029||value==0x202f||
    value==0x205f||value==0x3000||value==0xfeff;
}
std::string name(const Json& value) {
  if(!value.is_string())fail("name must be a string");
  const auto& text=value.get_ref<const std::string&>();
  std::size_t units=0,first=text.size(),last=0;
  // JSON parsing has already checked UTF-8 and surrogate pairs. Count JS's
  // UTF-16 units before trimming, then retain the original UTF-8 spelling.
  for(std::size_t at=0;at<text.size();) {
    const auto begin=at;auto byte=static_cast<unsigned char>(text[at++]);
    unsigned tail=0;std::uint32_t point=byte;
    if(byte>=0xf0){tail=3;point=byte&7;}else if(byte>=0xe0){tail=2;point=byte&15;}else if(byte>=0xc0){tail=1;point=byte&31;}
    for(unsigned i=0;i<tail;++i)point=(point<<6)|(static_cast<unsigned char>(text[at++])&63);
    units+=point>0xffff?2:1;
    if(!point)fail("name cannot contain NUL");
    if(!whitespace(point)){first=std::min(first,begin);last=at;}
  }
  if(units>80||!last)fail("name must contain1-80 UTF-16 units and non-whitespace text");
  return text.substr(first,last-first);
}
PcmCycle pcm(const Json& value,const char* label) {
  if(!value.is_array()||value.size()!=112)fail(std::string(label)+" requires112 PCM samples");
  PcmCycle output{};
  for(std::size_t i=0;i<output.size();++i)output[i]=static_cast<float>(number(value[i],-1,1,label));
  return output;
}
MorphEndpoints webEndpoints() {
  constexpr double pi=3.14159265358979323846264338327950288;
  MorphEndpoints endpoints;
  for(unsigned i=0;i<112;++i) {
    const double phase=double(i)/112;
    endpoints.a[i]=static_cast<float>(.65*std::sin(2*pi*phase));
    endpoints.b[i]=static_cast<float>(.65*(2*phase-1));
  }
  return endpoints;
}
WaveformShape waveform(const Json& value) {
  if(!value.is_string())fail("PMON waveform must be a string");
  const std::array<const char*,4> names{{"sine","triangle","saw","square"}};
  for(unsigned i=0;i<names.size();++i)if(value==names[i])return static_cast<WaveformShape>(i);
  fail("unknown PMON waveform");
}
ReverbPreset reverbPreset(const Json& value) {
  if(!value.is_string())fail("Reverb preset must be a string");
  const std::array<const char*,8> names{{"off","room","studio-small","studio-medium","studio-large","hall","space-echo","half-echo"}};
  for(unsigned i=0;i<names.size();++i)if(value==names[i])return static_cast<ReverbPreset>(i);
  fail("unknown Reverb preset");
}
struct Parsed { Json value; std::optional<std::string> pcmBase64; };
Parsed parse(std::string_view text) {
  if(text.empty()||text.size()>kMaxWebInstrumentBytes)fail("JSON size exceeds the48MiB bound or is empty");
  if(text.find('\0')!=std::string_view::npos)fail("JSON cannot contain literal NUL bytes");
  std::vector<std::set<std::string>> objects;std::size_t nodes=0;
  std::array<std::string,18> currentKey{};bool sampleObject=false;
  Parsed result;
  const Json::parser_callback_t callback=[&](int depth,Json::parse_event_t event,Json& parsed) {
    if(depth>16)fail("JSON nesting exceeds16 levels");
    if(event==Json::parse_event_t::object_start||event==Json::parse_event_t::array_start||event==Json::parse_event_t::value)
      if(++nodes>530000)fail("JSON contains too many values");
    if(event==Json::parse_event_t::object_start) {
      objects.emplace_back();
      if(depth==1)sampleObject=currentKey[1]=="sample";
    }
    if(event==Json::parse_event_t::object_end) {objects.pop_back();if(depth==1)sampleObject=false;}
    if(event==Json::parse_event_t::key) {
      const auto& key=parsed.get_ref<const std::string&>();
      if(key.size()>64||objects.empty()||!objects.back().insert(key).second)fail("duplicate or oversized JSON object key");
      currentKey.at(static_cast<std::size_t>(depth))=key;
    }
    if(event==Json::parse_event_t::value&&parsed.is_string()) {
      auto& string=parsed.get_ref<std::string&>();
      if(depth==2&&sampleObject&&currentKey[2]=="pcm16Base64") {
        if(string.size()>((kMaxSoftwarePcmBytes+2)/3)*4)fail("Software PCM exceeds32MiB");
        // Keep the large string out of the DOM: callback values otherwise copy
        // into their parent. Only this exact root.sample field gets the bound.
        result.pcmBase64=std::move(string);parsed=nullptr;
      } else if(string.size()>1024)fail("JSON string exceeds its bound");
    }
    return true;
  };
  try{result.value=Json::parse(text.begin(),text.end(),callback,true,false);return result;}
  catch(const Json::exception& error){fail(std::string("invalid JSON: ")+error.what());}
}
}

ImportedWebInstrument readWebInstrument(std::string_view text) {
  auto parsed=parse(text);const auto& value=parsed.value;
  keys(value,{"schema","version","name","preset","sample","reverb","waveform","pmon","noise","morph"},"instrument");
  if(member(value,"schema")!="tetorica-spu-synth.instrument")fail("unknown instrument schema");
  const double version=number(member(value,"version"),1,2,"version",true);
  if(version==1&&text.size()>kMaxWebV1InstrumentBytes)fail("version1 JSON exceeds8MiB");
  ImportedWebInstrument imported;imported.name=name(member(value,"name"));
  RawAdpcmSample sample;sample.name=imported.name;
  const auto& preset=member(value,"preset");
  keys(preset,{"volumeLeft","volumeRight","adsr1","adsr2","pitch","reverb"},"voice");
  sample.volumeLeft=number(member(preset,"volumeLeft"),-1,1,"left voice volume");
  sample.volumeRight=number(member(preset,"volumeRight"),-1,1,"right voice volume");
  sample.envelope.adsr1=static_cast<std::uint16_t>(number(member(preset,"adsr1"),0,65535,"ADSR1",true));
  sample.envelope.adsr2=static_cast<std::uint16_t>(number(member(preset,"adsr2"),0,65535,"ADSR2",true));
  sample.inspectorPitch=static_cast<std::uint16_t>(number(member(preset,"pitch"),0,16383,"inspector pitch",true));
  sample.reverb=boolean(member(preset,"reverb"),"voice Reverb send");
  const auto& data=member(value,"sample");
  SoftwarePcmSample software;
  if(version==1) {
    keys(data,{"adpcm","baseFrequencyHz","loopStartSamples"},"sample");
    const auto& bytes=member(data,"adpcm");
    if(!bytes.is_array()||bytes.empty()||bytes.size()>520192||bytes.size()%16)fail("ADPCM requires complete16-byte packets within520192 bytes");
    sample.adpcm.resize(bytes.size());
    for(std::size_t i=0;i<bytes.size();++i)sample.adpcm[i]=static_cast<std::uint8_t>(number(bytes[i],0,255,"ADPCM byte",true));
    sample.baseFrequencyHz=number(member(data,"baseFrequencyHz"),0,std::numeric_limits<double>::max(),"base frequency");
    if(sample.baseFrequencyHz<=0)fail("base frequency must be positive");
    if(data.contains("loopStartSamples"))sample.loopStartSamples=static_cast<std::uint32_t>(number(data["loopStartSamples"],0,910335,"loop position",true));
    if(value.contains("waveform")&&!value["waveform"].is_null())sample.waveform=pcm(value["waveform"],"waveform");
  } else {
    keys(data,{"pcm16Base64","baseFrequencyHz","loop"},"sample");
    member(data,"pcm16Base64");
    if(!parsed.pcmBase64)fail("pcm16Base64 must be a string");
    software.baseFrequencyHz=number(member(data,"baseFrequencyHz"),1.e-6,1.e6,"base frequency");
    software.loop=boolean(member(data,"loop"),"Software PCM loop");
    software.volumeLeft=sample.volumeLeft;software.volumeRight=sample.volumeRight;
    software.envelope=sample.envelope;software.inspectorPitch=sample.inspectorPitch;
    software.reverb=sample.reverb;software.name=imported.name;
    if(value.contains("waveform")&&!value["waveform"].is_null())fail("version2 cannot contain a waveform");
  }
  auto& state=imported.state;const auto& effects=member(value,"reverb");
  keys(effects,{"preset","volumeLeft","volumeRight"},"Reverb");
  state.reverb.preset=reverbPreset(member(effects,"preset"));
  state.reverb.enabled=state.reverb.preset!=ReverbPreset::Off;state.reverb.useToneSend=true;
  state.reverb.volumeLeft=number(member(effects,"volumeLeft"),0,1,"left Reverb return");
  state.reverb.volumeRight=number(member(effects,"volumeRight"),0,1,"right Reverb return");
  if(value.contains("pmon")) {
    const auto& settings=value["pmon"];keys(settings,{"enabled","ratio","depth","decayMs","waveform"},"PMON");
    if(settings.contains("enabled"))state.pmon.enabled=boolean(settings["enabled"],"PMON enabled");
    if(settings.contains("waveform"))state.pmon.waveform=waveform(settings["waveform"]);
    if(settings.contains("ratio"))state.pmon.ratio=number(settings["ratio"],.01,16,"PMON ratio");
    if(settings.contains("depth"))state.pmon.depth=number(settings["depth"],0,1,"PMON depth");
    if(settings.contains("decayMs"))state.pmon.decayMs=number(settings["decayMs"],0,5000,"PMON decay");
  }
  if(value.contains("noise")) {
    const auto& settings=value["noise"];keys(settings,{"enabled","clock"},"Noise");
    if(settings.contains("enabled"))state.noise.enabled=boolean(settings["enabled"],"Noise enabled");
    if(settings.contains("clock"))state.noise.clock=static_cast<std::uint32_t>(number(settings["clock"],0,63,"Noise clock",true));
  }
  if(state.pmon.enabled&&state.noise.enabled)fail("Web instruments cannot enable Noise and PMON together");
  auto endpoints=webEndpoints();
  if(value.contains("morph")) {
    const auto& settings=value["morph"];keys(settings,{"enabled","amount","a","b"},"Morph");
    if(settings.contains("enabled"))state.morph.enabled=boolean(settings["enabled"],"Morph enabled");
    if(settings.contains("amount"))state.morph.amount=number(settings["amount"],0,1,"Morph amount");
    if(settings.contains("a"))endpoints.a=pcm(settings["a"],"Morph A");
    if(settings.contains("b"))endpoints.b=pcm(settings["b"],"Morph B");
  }
  if(version==2) {
    if(state.morph.enabled)fail("Software audio does not support cycle Morph");
    software.pcm=decodeSoftwarePcmBase64(*parsed.pcmBase64);
    parsed.pcmBase64.reset();
    validateSoftwarePcmSample(software);
    state.definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromSoftwarePcmSample(software),endpoints);
  } else {
    if(state.morph.enabled&&(!sample.waveform||blendMorph(endpoints,state.morph.amount)!=*sample.waveform))
      fail("enabled Morph must match the saved PCM waveform");
    validateRawAdpcmSample(sample);
    state.definition=InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::fromAdpcmSample(sample),endpoints);
  }
  return imported;
}
} // namespace tetorica
