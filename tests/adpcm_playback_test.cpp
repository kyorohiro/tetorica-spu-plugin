// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "AdpcmPlayback.h"
#include "InstrumentState.h"
#include "SpuExportSource.h"
#include "SpuInstrument.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace tetorica;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F action){bool threw=false;try{action();}catch(const std::exception&){threw=true;}check(threw,"invalid metadata accepted");}
RawAdpcmSample customLoop(){
  auto sample=extractAdpcmSource(*InstrumentDefinition::builtin());
  sample.adpcm[1]=0;sample.adpcm[17]=6;sample.loopStartSamples=28;
  sample.name="Signed source 音";sample.volumeLeft=-.375;sample.volumeRight=-0.;
  sample.envelope={0x8f4a,0xe789};sample.inspectorPitch=0x1234;sample.reverb=true;
  validateRawAdpcmSample(sample);return sample;
}
void sameExceptFlags(const RawAdpcmSample& original,const RawAdpcmSample& edited){
  auto expected=edited;expected.adpcm=original.adpcm;expected.loopStartSamples=original.loopStartSamples;
  expected.waveform=original.waveform;
  check(sameRawAdpcmSample(expected,original),"playback edit changed non-loop metadata");
  for(std::size_t i=0;i<original.adpcm.size();++i)
    if(i%16!=1)check(original.adpcm[i]==edited.adpcm[i],"playback edit changed ADPCM data or predictor");
}
void rootEdits(){
  const auto source=customLoop();
  for(int note:{0,60,69,127}){
    const auto rooted=withAdpcmRoot(source,note);
    auto expected=source;expected.baseFrequencyHz=440.*std::pow(2.,(note-69)/12.);
    check(sameRawAdpcmSample(rooted,expected),"root edit changed bytes, custom loop or authored metadata");
  }
  rejects([&]{withAdpcmRoot(source,-1);});rejects([&]{withAdpcmRoot(source,128);});
  auto invalid=source;invalid.adpcm[1]=1;
  rejects([&]{withAdpcmRoot(invalid,60);});rejects([&]{withAdpcmPlayback(invalid,true);});
  auto invalidRoot=source;invalidRoot.baseFrequencyHz=0;
  rejects([&]{withAdpcmRoot(invalidRoot,60);});
  check(sameRawAdpcmSample(source,customLoop()),"metadata helper mutated its input");
}
void playbackEdits(){
  const auto source=customLoop();
  const auto shot=withAdpcmPlayback(source,false);sameExceptFlags(source,shot);
  check(!shot.loopStartSamples,"one-shot retained loop position");
  for(std::size_t offset=0;offset<shot.adpcm.size();offset+=16)
    check(shot.adpcm[offset+1]==(offset+16==shot.adpcm.size()?1:0),"one-shot flags are inconsistent");
  const auto loop=withAdpcmPlayback(source,true);sameExceptFlags(source,loop);
  check(loop.loopStartSamples==std::optional<std::uint32_t>(0),"loop did not start at frame zero");
  for(std::size_t offset=0;offset<loop.adpcm.size();offset+=16)
    check(loop.adpcm[offset+1]==(offset==0?6:offset+16==loop.adpcm.size()?3:0),"loop flags are inconsistent");
  check(sameRawAdpcmSample(loop,withAdpcmPlayback(loop,true)),"loop edit not idempotent");
  check(sameRawAdpcmSample(shot,withAdpcmPlayback(shot,false)),"one-shot edit not idempotent");
  auto single=shot;single.adpcm.resize(16);single.adpcm[1]=1;
  const auto singleLoop=withAdpcmPlayback(single,true);
  check(singleLoop.adpcm[1]==7&&singleLoop.loopStartSamples==std::optional<std::uint32_t>(0),"single packet loop needs flag7");
  check(withAdpcmPlayback(singleLoop,false).adpcm[1]==1,"single packet one-shot needs flag1");
}
void waveformAndState(){
  const auto source=extractAdpcmSource(*InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle)));
  check(source.waveform.has_value(),"fixture has no authored PCM");
  const auto loop=withAdpcmPlayback(source,true);
  check(sameRawAdpcmSample(source,loop),"loop0 edit lost canonical PCM or original bytes");
  const auto shot=withAdpcmPlayback(source,false);
  check(!shot.waveform&&shot.baseFrequencyHz==source.baseFrequencyHz,"one-shot retained incompatible waveform");
  const auto root=withAdpcmRoot(source,60);
  check(!root.waveform&&root.adpcm==source.adpcm&&root.loopStartSamples==source.loopStartSamples,"root edit lost ADPCM or retained incompatible waveform");
  const auto definition=InstrumentDefinition::fromAdpcmSample(root);
  const auto wire=encodeState(definition,37);
  const auto restored=decodeState({wire.data(),wire.size()});
  check(restored.gainPercent==37&&sameRawAdpcmSample(root,restored.definition->rawAdpcmSample()),"metadata did not survive state8");
  auto engine=prepareDefinition(*restored.definition);
  check(engine->prepare(48000),"restored metadata engine failed");
  engine->noteOn(60,127);std::vector<float> left(8192),right(8192);
  engine->render(left.data(),right.data(),static_cast<int>(left.size()));
  float peak=0;for(float value:left)peak=std::max(peak,std::abs(value));
  check(engine->healthy()&&peak>.005,"restored metadata was silent");
}
}
int main(){try{rootEdits();playbackEdits();waveformAndState();std::cout<<"PASS ADPCM root/custom-loop preservation, loop0/one-shot flags, single-packet flags, waveform consistency and state8/native restoration\n";}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
