// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WavImport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>

#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
using namespace tetorica;
namespace {
using Bytes=std::vector<std::uint8_t>;
constexpr double pi=3.14159265358979323846264338327950288;
void word(Bytes& b,std::size_t at,std::uint32_t v,unsigned count=4){for(unsigned n=0;n<count;++n)b.at(at+n)=static_cast<std::uint8_t>(v>>(n*8));}
void append(Bytes& b,std::uint32_t v,unsigned count=4){const auto at=b.size();b.resize(at+count);word(b,at,v,count);}
void text(Bytes& b,const char* s){for(unsigned n=0;n<4;++n)b.push_back(static_cast<std::uint8_t>(s[n]));}
void chunk(Bytes& b,const char* id,const Bytes& payload){text(b,id);append(b,static_cast<std::uint32_t>(payload.size()));b.insert(b.end(),payload.begin(),payload.end());if(payload.size()%2)b.push_back(0);}
void size(Bytes& b){word(b,4,static_cast<std::uint32_t>(b.size()-8));}
Bytes wave(const std::vector<float>& interleaved,unsigned channels=1,unsigned rate=44100,bool floating=false,bool fact=true) {
  CHECK(channels&&interleaved.size()%channels==0);Bytes b;text(b,"RIFF");append(b,0);text(b,"WAVE");
  Bytes fmt;append(fmt,floating?3:1,2);append(fmt,channels,2);append(fmt,rate);append(fmt,rate*channels*(floating?4:2));
  append(fmt,channels*(floating?4:2),2);append(fmt,floating?32:16,2);if(floating)append(fmt,0,2);chunk(b,"fmt ",fmt);
  if(floating&&fact){Bytes f;append(f,static_cast<std::uint32_t>(interleaved.size()/channels));chunk(b,"fact",f);}
  Bytes data;data.reserve(interleaved.size()*(floating?4:2));
  for(float v:interleaved) {
    if(floating){std::uint32_t bits;std::memcpy(&bits,&v,4);append(data,bits);}
    else append(data,static_cast<std::uint32_t>(std::clamp<long>(std::lround(double(v)*32768),-32768,32767))&65535,2);
  }
  chunk(b,"data",data);size(b);return b;
}
WavImportResult load(const Bytes& b,WavImportOptions options={}){return importWav({b.data(),b.size()},"Original WAV 音",options);}
template<class F>void rejects(F&& f){bool rejected=false;try{f();}catch(const std::invalid_argument&){rejected=true;}CHECK(rejected);}
std::vector<float> tone(std::size_t frames,unsigned rate,double frequency,double amplitude=.6) {
  std::vector<float> out(frames);for(std::size_t i=0;i<frames;++i)out[i]=static_cast<float>(amplitude*std::sin(2*pi*frequency*i/rate));return out;
}
// Independent raw decoder: no SDK, encoder helpers, envelopes or Gaussian.
std::vector<double> decode(const RawAdpcmSample& source,unsigned passes=1) {
  constexpr int filters[5][2]={{0,0},{60,0},{115,-52},{98,-55},{122,-60}};
  std::vector<double> out;int previous=0,previous2=0;
  for(unsigned pass=0;pass<passes;++pass)for(std::size_t at=0;at<source.adpcm.size();at+=16) {
    const auto header=source.adpcm[at];CHECK((header>>4)<5&&(header&15)<=12);
    for(unsigned i=0;i<28;++i) {
      int nibble=(source.adpcm[at+2+i/2]>>((i%2)*4))&15;if(nibble>=8)nibble-=16;
      const int sum=previous*filters[header>>4][0]+previous2*filters[header>>4][1]+32;
      const int predicted=static_cast<int>(std::floor(double(sum)/64));
      const int value=nibble*(4096>>(header&15))+predicted;CHECK(value>=-32768&&value<=32767);
      previous2=previous;previous=value;out.push_back(double(value)/32768);
    }
  }
  return out;
}
double rms(const std::vector<double>& pcm,std::size_t begin,std::size_t end) {
  double sum=0;for(std::size_t i=begin;i<end;++i)sum+=pcm[i]*pcm[i];return std::sqrt(sum/(end-begin));
}
double amplitude(const std::vector<double>& pcm,double frequency,std::size_t begin,std::size_t end) {
  double s=0,c=0;for(std::size_t i=begin;i<end;++i){s+=pcm[i]*std::sin(2*pi*frequency*i/44100);c+=pcm[i]*std::cos(2*pi*frequency*i/44100);}
  return 2*std::sqrt(s*s+c*c)/(end-begin);
}
void formatsAndEncoding() {
  auto input=tone(4410,44100,261.6255653005986);const auto bytes=wave(input);
  const auto result=load(bytes);const auto& sample=result.sample;
  CHECK(result.sampleRate==44100&&result.channels==1&&result.sourceFrames==4410&&result.resampledFrames==4410&&result.paddedFrames==14);
  CHECK(sample.adpcm.size()==2528&&!sample.loopStartSamples&&sample.adpcm[1]==0&&sample.adpcm[sample.adpcm.size()-15]==1);
  CHECK(sample.baseFrequencyHz==440*std::pow(2.,-9./12)&&sample.name=="Original WAV 音"&&!sample.waveform);
  CHECK(sample.volumeLeft==.75&&sample.volumeRight==.75&&sample.envelope.adsr1==0x000f&&sample.envelope.adsr2==0x1fc0&&!sample.reverb);
  const auto decoded=decode(sample);double error=0,signal=0;
  for(std::size_t i=0;i<input.size();++i){const double difference=decoded[i]-input[i];error+=difference*difference;signal+=double(input[i])*input[i];}
  CHECK(10*std::log10(signal/error)>35);
  auto mutableBytes=bytes;const auto owned=load(mutableBytes);std::fill(mutableBytes.begin(),mutableBytes.end(),0);
  CHECK(sameRawAdpcmSample(owned.sample,sample));
  const auto looped=load(bytes,{69,true});CHECK(looped.sample.baseFrequencyHz==440&&looped.sample.loopStartSamples==0);
  CHECK(looped.sample.adpcm[1]==6&&looped.sample.adpcm[looped.sample.adpcm.size()-15]==3&&looped.paddedFrames==14);
  const auto loops=decode(looped.sample,2);CHECK(std::equal(loops.begin(),loops.begin()+decoded.size(),loops.begin()+decoded.size()));
  const auto shortResult=load(wave(std::vector<float>(7,.25f)),{0,true});
  CHECK(shortResult.paddedFrames==21&&shortResult.sample.adpcm.size()==16&&shortResult.sample.adpcm[1]==7);
  CHECK(shortResult.sample.baseFrequencyHz==440*std::pow(2.,-69./12));
  const auto shortDecoded=decode(shortResult.sample);for(unsigned i=0;i<28;++i)CHECK(shortDecoded[i]==(i<7?.25:0));
  const auto highest=load(wave({0}),{127,false});CHECK(highest.sample.baseFrequencyHz==440*std::pow(2.,58./12));
  std::vector<float> stereo(112*2);for(std::size_t i=0;i<112;++i){stereo[i*2]=.25f;stereo[i*2+1]=.75f;}
  const auto averaged=load(wave(stereo,2,44100,true));CHECK(averaged.channels==2);
  for(double value:decode(averaged.sample))CHECK(value==.5);
  for(std::size_t i=0;i<112;++i)stereo[i*2+1]=-.25f;
  const auto cancelled=load(wave(stereo,2,44100,true,false));for(double value:decode(cancelled.sample))CHECK(value==0);
  auto float16=wave(stereo,2,44100,true,false);float16.erase(float16.begin()+36,float16.begin()+38);word(float16,16,16);size(float16);
  CHECK(sameRawAdpcmSample(load(float16).sample,cancelled.sample));
  // Ancillary chunks with odd padding, arbitrary order, and fact on PCM are
  // traversed rather than assuming the familiar 44-byte preamble.
  auto unusual=bytes;Bytes prefix;chunk(prefix,"JUNK",{1,2,3});unusual.insert(unusual.begin()+12,prefix.begin(),prefix.end());size(unusual);
  CHECK(sameRawAdpcmSample(load(unusual).sample,sample));
  auto reversed=bytes;std::rotate(reversed.begin()+12,reversed.begin()+36,reversed.end());size(reversed);
  CHECK(sameRawAdpcmSample(load(reversed).sample,sample));
}
void resampling() {
  for(unsigned rate:{8000,11025,22050,48000,96000,192000,384000}) {
    const auto result=load(wave(tone(rate/10,rate,1000),1,rate,true));const auto pcm=decode(result.sample);
    const auto expected=(std::uint64_t(rate/10)*44100+rate-1)/rate;
    CHECK(result.sourceFrames==rate/10&&result.resampledFrames==expected&&result.paddedFrames==(expected+27)/28*28-expected);
    CHECK(std::abs(amplitude(pcm,1000,560,3920)-.6)<.012);
  }
  auto result=load(wave(tone(19200,96000,30000),1,96000,true));const auto aliased=decode(result.sample);
  CHECK(rms(aliased,560,8260)<.0015); // 30 kHz must not fold audibly to 14.1 kHz.
  result=load(wave(tone(4000,8000,800),1,8000,true));const auto upsampled=decode(result.sample);
  CHECK(std::abs(amplitude(upsampled,800,560,21560)-.6)<.01&&amplitude(upsampled,7200,560,21560)<.002);
  const auto duration=load(wave({.25f,.25f,.25f},1,48000,true));CHECK(duration.resampledFrames==3&&duration.paddedFrames==25);
  for(double v:decode(duration.sample))CHECK(v==.25||v==0);
}
void malformedAndBounds() {
  const auto good=wave(tone(112,44100,440));
  for(std::size_t length=0;length<good.size();++length) {
    // Truncation with an honest outer size must still fail structural/data
    // checks except when a complete frame-aligned data chunk is deliberately
    // rebuilt by the writer; a partial original chunk never qualifies.
    Bytes bad(good.begin(),good.begin()+length);if(bad.size()>=8)size(bad);rejects([&]{load(bad);});
  }
  for(unsigned field=0;field<16;++field) {
    auto bad=good;
    switch(field){
      case 0:bad[0]='X';break;case 1:bad[8]='X';break;case 2:word(bad,4,0xffffffff);break;
      case 3:word(bad,16,0xffffffff);break;case 4:word(bad,20,0xfffe,2);break;case 5:word(bad,22,3,2);break;
      case 6:word(bad,24,0);break;case 7:word(bad,24,7999);break;case 8:word(bad,24,384001);break;
      case 9:word(bad,28,0);break;case 10:word(bad,32,4,2);break;case 11:word(bad,34,24,2);break;
      case 12:word(bad,40,1);break;case 13:word(bad,40,0);bad.resize(44);size(bad);break;
      case 14:bad.push_back(0);size(bad);break;case 15:bad.insert(bad.end(),good.begin()+12,good.end());size(bad);break;
    }
    rejects([&]{load(bad);});
  }
  for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),1.001f,-1.001f})
    rejects([&]{load(wave({0,invalid},2,44100,true));});
  auto badFloat=wave({0},1,44100,true);word(badFloat,36,2,2);rejects([&]{load(badFloat);});
  badFloat=wave({0},1,44100,true);word(badFloat,46,2);rejects([&]{load(badFloat);});
  auto missingPad=good;Bytes odd;chunk(odd,"JUNK",{1});missingPad.insert(missingPad.end(),odd.begin(),odd.end()-1);size(missingPad);rejects([&]{load(missingPad);});
  rejects([&]{load(good,{-1,false});});rejects([&]{load(good,{128,false});});
  rejects([&]{importWav({good.data(),good.size()},std::string("bad\0name",8));});
  rejects([&]{importWav({nullptr,good.size()},"null");});
  std::uint8_t dummy=0;rejects([&]{importWav({&dummy,kMaxWavFileBytes+1},"oversize");});
  constexpr auto maximum=vab::kMaxSampleBytes/16*28;
  const auto maxWav=wave(std::vector<float>(maximum));const auto maximumResult=load(maxWav);
  CHECK(maximumResult.sample.adpcm.size()==vab::kMaxSampleBytes&&maximumResult.paddedFrames==0);
  const auto tooLarge=wave(std::vector<float>(maximum+1));rejects([&]{load(tooLarge);});
  const auto expanded=wave(std::vector<float>(maximum/2+1),1,22050);rejects([&]{load(expanded);});
}
void binary(const std::filesystem::path& path,const Bytes& bytes) {
  std::ofstream file(path,std::ios::binary);CHECK(file.good());file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));CHECK(file.good());
}
void fixtures(const std::filesystem::path& directory) {
  std::filesystem::create_directories(directory);std::ofstream manifest(directory/"manifest.json");CHECK(manifest.good());
  manifest<<"{\n  \"description\":\"Original synthetic WAV conversion fixtures; no game recordings. paddedFrames is added zero count.\",\n  \"cases\":[\n";
  struct Fixture{const char* name;Bytes bytes;WavImportOptions options;};
  std::vector<float> stereo(4800*2);for(std::size_t i=0;i<4800;++i){stereo[i*2]=static_cast<float>(.25*std::sin(2*pi*261.6255653005986*i/48000));stereo[i*2+1]=static_cast<float>(.75*std::sin(2*pi*329.6275569128699*i/48000));}
  const std::array<Fixture,4> cases{{{"pcm16-mono-c4",wave(tone(4410,44100,261.6255653005986)),{}},
    {"float32-stereo",wave(stereo,2,48000,true),{}},{"silence",wave(std::vector<float>(2205),1,22050),{}},
    {"short-loop",wave(std::vector<float>(7,.25f)),{69,true}}}};
  for(std::size_t i=0;i<cases.size();++i) {
    const auto& c=cases[i];const auto result=importWav({c.bytes.data(),c.bytes.size()},c.name,c.options);
    binary(directory/(std::string(c.name)+".wav"),c.bytes);binary(directory/(std::string(c.name)+".adpcm"),result.sample.adpcm);
    if(i)manifest<<",\n";
    manifest<<"    {\"wav\":\""<<c.name<<".wav\",\"adpcm\":\""<<c.name<<".adpcm\",\"rootMidiNote\":"<<c.options.rootMidiNote<<",\"loop\":"<<(c.options.loop?"true":"false")
      <<",\"sampleRate\":"<<result.sampleRate<<",\"channels\":"<<result.channels<<",\"sourceFrames\":"<<result.sourceFrames<<",\"resampledFrames\":"<<result.resampledFrames<<",\"paddedFrames\":"<<result.paddedFrames<<"}";
  }
  const auto invalid=wave({std::numeric_limits<float>::quiet_NaN()},1,44100,true);binary(directory/"invalid-float.wav",invalid);
  auto truncated=cases[0].bytes;truncated.pop_back();binary(directory/"invalid-truncated.wav",truncated);
  manifest<<",\n    {\"wav\":\"invalid-float.wav\",\"valid\":false},\n    {\"wav\":\"invalid-truncated.wav\",\"valid\":false}\n  ]\n}\n";CHECK(manifest.good());
}
}
int main(int argc,char** argv) {
  formatsAndEncoding();resampling();malformedAndBounds();
  if(argc==3&&std::string(argv[1])=="--write-fixtures")fixtures(std::filesystem::u8path(argv[2]));else CHECK(argc==1);
  std::puts("PASS Native-free WAV PCM16/Float32/mono/stereo/chunks/bounds/downmix/root/wholeloop/padding; independent ADPCM decode SNR+repeat; 8k..384k resampling frequency/alias; full sample RAM; malformed inputs and no truncation");
}
