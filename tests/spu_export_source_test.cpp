// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuExportSource.h"
#include "SpuBuiltin.h"
#include "InstrumentState.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)
using namespace tetorica;
namespace {
using Def=InstrumentDefinition;
using Bytes=std::vector<std::uint8_t>;
Bytes fixture() {
  std::ifstream file(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
  CHECK(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
bool sameCycle(const PcmCycle& a,const PcmCycle& b) {
  return std::memcmp(a.data(),b.data(),sizeof(a))==0;
}
void defaults(const RawAdpcmSample& source) {
  CHECK(source.baseFrequencyHz==44100./112&&source.loopStartSamples==0);
  CHECK(source.volumeLeft==1&&source.volumeRight==1);
  CHECK(source.envelope.adsr1==0x000f&&source.envelope.adsr2==0x1fc0);
  CHECK(!source.reverb&&source.inspectorPitch==0x1000&&source.name.empty());
}
void builtinAndPcm() {
  const auto builtin=Def::builtin();auto raw=extractAdpcmSource(*builtin);
  defaults(raw);validateRawAdpcmSample(raw);CHECK(!raw.waveform);
  CHECK(raw.adpcm==Bytes(kBuiltinSineAdpcm.begin(),kBuiltinSineAdpcm.end()));
  // The original fixed bytes also match the current cyclic encoder. The
  // engine and source extraction share the fixed literal, independent of a
  // future encoder revision; the existing engine WAV remains the PCM oracle.
  CHECK(encodePcmCycle(makePcmCycle(WaveformShape::Sine))==kBuiltinSineAdpcm);
  raw.adpcm[2]^=1;CHECK(!sameRawAdpcmSample(raw,extractAdpcmSource(*builtin)));

  for(auto shape:{WaveformShape::Sine,WaveformShape::Triangle,WaveformShape::Saw,WaveformShape::Square}) {
    auto pcm=makePcmCycle(shape);const auto authored=pcm;
    const auto definition=Def::fromPcmCycle(pcm);pcm.fill(0);
    const auto source=extractAdpcmSource(*definition);defaults(source);
    const auto encoded=encodePcmCycle(authored);
    CHECK(source.adpcm==Bytes(encoded.begin(),encoded.end()));
    CHECK(source.waveform&&sameCycle(*source.waveform,authored));validateRawAdpcmSample(source);
  }
  auto custom=makePcmCycle(WaveformShape::Triangle);custom[0]=-0.f;custom[37]=.1234567f;
  const auto def=Def::fromPcmCycle(custom);auto source=extractAdpcmSource(*def);
  CHECK(source.waveform&&sameCycle(*source.waveform,custom));
  (*source.waveform)[0]=0.f;source.adpcm[2]^=1;
  const auto again=extractAdpcmSource(*def);CHECK(sameCycle(*again.waveform,custom));
  CHECK(!sameRawAdpcmSample(source,again));
  MorphEndpoints endpoints;endpoints.a=makePcmCycle(WaveformShape::Square);endpoints.b=custom;
  const auto edited=Def::withMorphEndpoints(def,endpoints);
  CHECK(sameRawAdpcmSample(again,extractAdpcmSource(*edited)));
}
void vabSelection() {
  const auto bytes=fixture();const auto bank=vab::parse({bytes.data(),bytes.size()});
  CHECK(bank.tones.size()==3);
  for(std::size_t flat=0;flat<bank.tones.size();++flat) {
    const auto expected=vab::prepareTone(bank,flat);const auto& tone=bank.tones[flat];
    const auto definition=Def::fromVab(bytes,tone.program,tone.index);
    auto source=extractAdpcmSource(*definition);validateRawAdpcmSample(source);
    CHECK(source.adpcm==expected.adpcm&&source.adpcm.size()<bytes.size());
    CHECK(source.baseFrequencyHz==expected.rootFrequencyHz&&source.loopStartSamples==expected.loopStartSamples);
    CHECK(source.envelope.adsr1==expected.adsr1&&source.envelope.adsr2==expected.adsr2);
    CHECK(source.volumeLeft==expected.volumeLeft&&source.volumeRight==expected.volumeRight&&source.reverb==expected.reverb);
    CHECK(source.inspectorPitch==0x1000&&source.name.empty()&&!source.waveform);
    source.adpcm[2]^=1;CHECK(extractAdpcmSource(*definition).adpcm==expected.adpcm);
    const auto flatDefinition=Def::fromVabFlatIndex(bytes,flat);
    CHECK(sameRawAdpcmSample(extractAdpcmSource(*definition),extractAdpcmSource(*flatDefinition)));
  }
}
void rawExactCopy() {
  RawAdpcmSample raw;raw.adpcm.assign(kBuiltinSineAdpcm.begin(),kBuiltinSineAdpcm.end());
  raw.loopStartSamples=0;raw.baseFrequencyHz=123.456789012345;raw.volumeLeft=-.712345678;raw.volumeRight=-0.;
  raw.envelope={0xc4b7,0x3fda};raw.reverb=true;raw.inspectorPitch=12345;raw.name="原本 ADPCM 🎵";
  const auto definition=Def::fromAdpcmSample(raw);auto source=extractAdpcmSource(*definition);
  CHECK(sameRawAdpcmSample(raw,source));source.adpcm[2]^=1;source.volumeRight=0.;source.name.clear();
  CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*definition)));
  MorphEndpoints endpoints;endpoints.a.fill(.3f);endpoints.b.fill(-.3f);
  CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*Def::withMorphEndpoints(definition,endpoints))));
  raw.baseFrequencyHz=44100./112;raw.waveform=makePcmCycle(WaveformShape::Sine);
  const auto authored=Def::fromAdpcmSample(raw);CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*authored)));
  // An imported legacy encoder payload is never regenerated during extraction.
  constexpr std::array<std::uint8_t,64> legacy{{0,6,0,17,33,34,51,67,68,85,85,101,102,102,102,102,36,0,231,32,32,1,2,32,31,16,31,15,240,240,240,255,38,0,189,172,172,171,154,155,169,169,168,153,154,154,170,186,38,3,170,188,188,205,221,253,13,254,241,17,17,35,66,67}};
  raw.adpcm.assign(legacy.begin(),legacy.end());
  CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*Def::fromAdpcmSample(raw))));
  // Pure extraction preserves flag4-only repeat markers without rewriting the
  // original sample now that Native playback also supports that flag pattern.
  raw.waveform.reset();raw.adpcm.assign(kBuiltinSineAdpcm.begin(),kBuiltinSineAdpcm.end());
  raw.adpcm[1]=0;raw.adpcm[17]=4;raw.loopStartSamples=28;
  CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*Def::fromAdpcmSample(raw))));
  raw.adpcm.resize(vab::kMaxSampleBytes,0);raw.adpcm[17]=0;raw.adpcm[49]=0;
  raw.adpcm[raw.adpcm.size()-15]=1;raw.loopStartSamples.reset();raw.baseFrequencyHz=std::numeric_limits<double>::denorm_min();
  CHECK(sameRawAdpcmSample(raw,extractAdpcmSource(*Def::fromAdpcmSample(raw))));
}
}
int main() {
  builtinAndPcm();vabSelection();rawExactCopy();
  std::puts("PASS pure source extraction: fixed builtin, canonical/custom PCM, all sparse VAB Tones, exact raw/legacy metadata+signed phases+full sample RAM; immutable independent ownership; Morph endpoints do not replace original source");
}
