// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Independent binary mutations and a checked-in JavaScript-produced oracle.
#include "VabBank.h"
#include "fixtures/vab/expected.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef VAB_FIXTURE_DIR
#error VAB_FIXTURE_DIR must name the checked-in tests/fixtures/vab directory
#endif

namespace {
namespace vab = tetorica::vab;
using Bytes = std::vector<std::uint8_t>;
std::size_t assertions = 0, rejected = 0;
#define CHECK(value) do { ++assertions; if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::exit(1); } } while (0)
constexpr std::size_t kHeaderBytes = 3616;
constexpr std::size_t kSizeTable = 0xc20;
constexpr std::size_t kFirstProgram = 0x20 + 3 * 16;
constexpr std::size_t kSecondProgram = 0x20 + 11 * 16;

Bytes fixture(const char* name) {
  std::ifstream file(std::string(VAB_FIXTURE_DIR) + "/" + name, std::ios::binary);
  CHECK(file.good());
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
vab::BytesView view(const Bytes& bytes) { return {bytes.data(), bytes.size()}; }
void word(Bytes& bytes, std::size_t at, unsigned value) {
  bytes.at(at) = static_cast<std::uint8_t>(value);
  bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}
void number(Bytes& bytes, std::size_t at, std::size_t value) {
  for (unsigned n = 0; n < 4; ++n) bytes.at(at + n) = static_cast<std::uint8_t>(value >> (8 * n));
}
template<class Function> void rejects(const char* description, Function function) {
  bool failed = false;
  try { function(); } catch (const std::exception&) { failed = true; }
  if (!failed) { std::fprintf(stderr, "Unexpected acceptance: %s\n", description); std::exit(1); }
  ++rejected; ++assertions;
}
void close(double actual, double expected) {
  CHECK(std::isfinite(actual));
  CHECK(std::abs(actual - expected) <= 1e-12 * std::max(1., std::abs(expected)));
}
void checkTone(const vab::Tone& actual, const vab_fixture::Tone& expected) {
#define FIELD(name) CHECK(actual.name == expected.name)
  FIELD(program); FIELD(index); FIELD(sample); FIELD(mode); FIELD(volume); FIELD(pan);
  FIELD(center); FIELD(shift); FIELD(keyLow); FIELD(keyHigh); FIELD(adsr1); FIELD(adsr2);
  FIELD(vibratoWidth); FIELD(vibratoTime); FIELD(portamentoWidth); FIELD(portamentoTime);
  FIELD(bendMin); FIELD(bendMax);
#undef FIELD
}
void checkMetadata(const vab::Bank& bank, unsigned version = vab_fixture::kVersion) {
  CHECK(bank.version == version && bank.id == vab_fixture::kId);
  CHECK(bank.masterVolume == vab_fixture::kMasterVolume && bank.masterPan == vab_fixture::kMasterPan);
  CHECK(bank.programs.size() == std::size(vab_fixture::kPrograms));
  CHECK(bank.tones.size() == std::size(vab_fixture::kTones));
  CHECK(bank.samples.size() == std::size(vab_fixture::kSamples));
  for (std::size_t n = 0; n < bank.programs.size(); ++n) {
    const auto& actual = bank.programs[n]; const auto& expected = vab_fixture::kPrograms[n];
    CHECK(actual.index == expected.index && actual.volume == expected.volume && actual.pan == expected.pan);
    CHECK(actual.toneIndices.size() == expected.toneCount);
    for (std::size_t t = 0; t < actual.toneIndices.size(); ++t)
      CHECK(actual.toneIndices[t] == expected.firstTone + t);
  }
  for (std::size_t n = 0; n < bank.tones.size(); ++n) checkTone(bank.tones[n], vab_fixture::kTones[n]);
  for (std::size_t n = 0; n < bank.samples.size(); ++n) {
    const auto& actual = bank.samples[n]; const auto& expected = vab_fixture::kSamples[n];
    CHECK(actual.index == expected.index && actual.bytes.size() == expected.size);
    CHECK(std::equal(actual.bytes.begin(), actual.bytes.end(), expected.bytes));
  }
}
void checkPrepared(const vab::PreparedTone& actual, const vab_fixture::Prepared& expected) {
  CHECK(actual.adpcm.size() == expected.size);
  CHECK(std::equal(actual.adpcm.begin(), actual.adpcm.end(), expected.adpcm));
  close(actual.rootFrequencyHz, expected.rootFrequencyHz);
  CHECK(actual.decodedFrames == expected.decodedFrames);
  CHECK(actual.loopStartSamples.has_value() == (expected.loopStartSamples >= 0));
  if (actual.loopStartSamples) CHECK(*actual.loopStartSamples == static_cast<unsigned>(expected.loopStartSamples));
  CHECK(actual.adsr1 == expected.adsr1 && actual.adsr2 == expected.adsr2);
  close(actual.volumeLeft, expected.volumeLeft); close(actual.volumeRight, expected.volumeRight);
  CHECK(actual.reverb == expected.reverb && actual.trimmedBytes == expected.trimmedBytes);
  checkTone(actual.tone, expected.tone);
}

void testFixtureAndOwnership(const Bytes& source, const Bytes& vh, const Bytes& vb) {
  CHECK(source.size() == 3744 && vh.size() == 3616 && vb.size() == 128);
  checkMetadata(vab::parse(view(source)));
  checkMetadata(vab::parse(view(vh), view(vb)));
  auto ownedBytes = source;
  auto ownedBank = vab::parse(view(ownedBytes));
  std::fill(ownedBytes.begin(), ownedBytes.end(), 0);
  checkMetadata(ownedBank);
  auto ownedHeader = vh, ownedBody = vb;
  auto ownedSplit = vab::parse(view(ownedHeader), view(ownedBody));
  std::fill(ownedHeader.begin(), ownedHeader.end(), 0);
  std::fill(ownedBody.begin(), ownedBody.end(), 0);
  checkMetadata(ownedSplit);
  auto selected = vab::prepareTone(ownedBank, 0);
  std::fill(ownedBank.samples[0].bytes.begin(), ownedBank.samples[0].bytes.end(), 0xff);
  checkPrepared(selected, vab_fixture::kPrepared[0]);

  Bytes backing(source.size() + 31, 0xa5);
  std::copy(source.begin(), source.end(), backing.begin() + 11);
  number(backing, 15, 6); // Version field inside a nonzero-offset view.
  checkMetadata(vab::parse({backing.data() + 11, source.size()}), 6);
  auto version6 = vh; number(version6, 4, 6);
  checkMetadata(vab::parse(view(version6), view(vb)), 6);

  auto padding = source;
  padding.insert(padding.end(), 24, 0xff);
  number(padding, 12, padding.size());
  checkMetadata(vab::parse(view(padding))); // Post-Sample body padding is not payload.
  auto prefix = source;
  std::fill(prefix.begin() + kHeaderBytes, prefix.begin() + kHeaderBytes + 16, 0xff);
  checkMetadata(vab::parse(view(prefix))); // Reserved prefix must not become Sample 1.
}

void testJavaScriptPreparations(const Bytes& source) {
  for (const auto& expected : vab_fixture::kPrepared) {
    auto bank = vab::parse(view(source));
    const std::string name = expected.name;
    if (name == "volumePanTuning") {
      bank.masterVolume = 91; bank.masterPan = 17;
      bank.programs[0].volume = 83; bank.programs[0].pan = 22;
      auto& tone = bank.tones[0];
      tone.volume = 76; tone.pan = 108; tone.center = 64; tone.shift = 37;
      tone.mode = 4; tone.adsr1 = 0x9f12; tone.adsr2 = 0xa4c2;
      tone.vibratoWidth = 5; tone.vibratoTime = 6;
      tone.portamentoWidth = 7; tone.portamentoTime = 8; tone.bendMin = 9; tone.bendMax = 10;
    } else if (name == "nonzeroLoop") {
      bank.samples[0].bytes[1] &= static_cast<std::uint8_t>(~4);
      bank.samples[0].bytes[17] |= 4;
    } else if (name == "loopFallback") {
      bank.samples[0].bytes[1] &= static_cast<std::uint8_t>(~4);
    } else if (name == "earlyEnd") {
      bank.samples[0].bytes[1] = 1;
    }
    checkPrepared(vab::prepareTone(bank, expected.flatIndex), expected);
  }
  auto bank = vab::parse(view(source));
  bank.programs[0].pan = 0; bank.tones[0].pan = 127; bank.tones[0].mode = 4;
  const auto opposed = vab::prepareTone(bank, 0);
  CHECK(opposed.volumeLeft == 0 && opposed.volumeRight == 0 && opposed.reverb);
  bank.masterVolume = 0;
  CHECK(vab::prepareTone(bank, 0).volumeLeft == 0);
}

void testMalformedInputs(const Bytes& source, const Bytes& vh, const Bytes& vb) {
  using Mutation = std::pair<const char*, std::function<void(Bytes&)>>;
  const std::vector<Mutation> mutations{
    {"signature", [](auto& b){b[0] = 0;}},
    {"version5", [](auto& b){number(b,4,5);}}, {"version8", [](auto& b){number(b,4,8);}},
    {"declared too small", [](auto& b){number(b,12,b.size()-1);}},
    {"declared too large", [](auto& b){number(b,12,b.size()+1);}},
    {"program count zero", [](auto& b){word(b,0x12,0);}},
    {"program count129", [](auto& b){word(b,0x12,129);}},
    {"tone count zero", [](auto& b){word(b,0x14,0);}},
    {"too many tones", [](auto& b){word(b,0x14,33);}},
    {"tone count mismatch down", [](auto& b){word(b,0x14,2);}},
    {"tone count mismatch up", [](auto& b){word(b,0x14,4);}},
    {"sample count256", [](auto& b){word(b,0x16,256);}},
    {"sample count zero with references", [](auto& b){word(b,0x16,0);}},
    {"sample count one with reference2", [](auto& b){word(b,0x16,1);}},
    {"master volume", [](auto& b){b[0x18]=128;}}, {"master pan", [](auto& b){b[0x19]=128;}},
    {"program count17 tones", [](auto& b){b[kFirstProgram]=17;}},
    {"missing sparse program", [](auto& b){b[kSecondProgram]=0;}},
    {"extra sparse program", [](auto& b){b[0x20+127*16]=1;}},
    {"program volume", [](auto& b){b[kFirstProgram+1]=128;}},
    {"program pan", [](auto& b){b[kFirstProgram+4]=128;}},
    {"tone volume", [](auto& b){b[0x822]=128;}}, {"tone pan", [](auto& b){b[0x823]=128;}},
    {"tone center", [](auto& b){b[0x824]=128;}}, {"tone shift", [](auto& b){b[0x825]=128;}},
    {"key minimum", [](auto& b){b[0x826]=128;}}, {"key maximum", [](auto& b){b[0x827]=128;}},
    {"inverted keys", [](auto& b){b[0x826]=97;}},
    {"sample reference", [](auto& b){word(b,0x820+22,3);}},
    {"prefix outside body", [](auto& b){word(b,kSizeTable,17);}},
    {"empty sample", [](auto& b){word(b,kSizeTable+2,0);}},
    {"short sample", [](auto& b){word(b,kSizeTable+2,1);}},
    {"sample1 outside body", [](auto& b){word(b,kSizeTable+2,65535);}},
    {"sample2 outside body", [](auto& b){word(b,kSizeTable+4,5);}},
  };
  for (const auto& mutation : mutations) {
    auto bytes = source; mutation.second(bytes);
    rejects(mutation.first, [&]{vab::parse(view(bytes));});
  }
  for (std::size_t length=0; length<source.size(); ++length)
    rejects("complete truncation", [&]{vab::parse({source.data(),length});});
  for (std::size_t length=0; length<vh.size(); ++length)
    rejects("VH truncation", [&]{vab::parse({vh.data(),length},view(vb));});
  for (std::size_t length=0; length<vb.size(); ++length)
    rejects("VB truncation", [&]{vab::parse(view(vh),{vb.data(),length});});
  rejects("complete input passed as VH", [&]{vab::parse(view(source),view(vb));});
  rejects("VB without VH", [&]{vab::parse(view(vb));});
  rejects("empty", [&]{vab::parse({});});
  rejects("null complete", [&]{vab::parse({nullptr,source.size()});});
  rejects("null header", [&]{vab::parse({nullptr,vh.size()},view(vb));});
  rejects("null body", [&]{vab::parse(view(vh),{nullptr,vb.size()});});
  Bytes huge(vab::kMaxFileBytes+1,0);
  rejects("file limit before payload allocation", [&]{vab::parse(view(huge));});
  huge.resize(vab::kMaxBodyBytes+1);
  auto header=vh; number(header,12,header.size()+huge.size());
  rejects("VB body limit", [&]{vab::parse(view(header),view(huge));});
}

void testSelectedSampleValidation(const Bytes& source) {
  using Mutation = std::pair<const char*, std::function<void(vab::Bank&)>>;
  const std::vector<Mutation> mutations{
    {"no own END", [](auto& b){b.samples[0].bytes[49]=0;}},
    {"predictor5", [](auto& b){b.samples[0].bytes[0]=0x50;}},
    {"shift13", [](auto& b){b.samples[0].bytes[0]=0x2d;}},
    {"unsupported flags", [](auto& b){b.samples[0].bytes[1]|=8;}},
    {"multiple markers", [](auto& b){b.samples[0].bytes[17]|=4;}},
    {"one shot marker", [](auto& b){b.samples[0].bytes[49]=1;}},
    {"empty sample", [](auto& b){b.samples[0].bytes.clear();}},
    {"non-unit sample length", [](auto& b){b.samples[0].bytes.pop_back();}},
  };
  for (const auto& mutation : mutations) {
    auto bank=vab::parse(view(source)); mutation.second(bank);
    rejects(mutation.first, [&]{vab::prepareTone(bank,0);});
  }
  auto badAdpcm=source;
  badAdpcm[kHeaderBytes+16+49]=0; // The next Sample has END, but is not ours.
  auto bounded=vab::parse(view(badAdpcm));
  rejects("never borrow next Sample END", [&]{vab::prepareTone(bounded,0);});
  checkPrepared(vab::prepareTone(bounded,2),vab_fixture::kPrepared[2]);
  auto ignored=vab::parse(view(source));
  std::fill(ignored.samples[0].bytes.begin()+64,ignored.samples[0].bytes.end(),0xff);
  checkPrepared(vab::prepareTone(ignored,0),vab_fixture::kPrepared[0]);
  // Format lengths use eight-byte units; padding after a complete END may be
  // half a packet. Retained ADPCM must still consist only of complete packets.
  auto halfPadding=vab::parse(view(source));
  halfPadding.samples[0].bytes.assign(24,0xff);
  halfPadding.samples[0].bytes[0]=0x4c; halfPadding.samples[0].bytes[1]=1;
  const auto half=vab::prepareTone(halfPadding,0);
  CHECK(half.adpcm.size()==16 && half.trimmedBytes==8 && half.decodedFrames==28);
  for (const auto header : {0x00,0x0c,0x40,0x4c}) {
    for (const auto flags : {1,3,7}) {
      auto boundaries=vab::parse(view(source));
      boundaries.samples[0].bytes.assign(16,0);
      boundaries.samples[0].bytes[0]=static_cast<std::uint8_t>(header);
      boundaries.samples[0].bytes[1]=static_cast<std::uint8_t>(flags);
      const auto retained=vab::prepareTone(boundaries,0);
      CHECK(retained.adpcm==boundaries.samples[0].bytes && retained.decodedFrames==28);
      CHECK(retained.loopStartSamples.has_value()==(flags!=1));
    }
  }
  auto flag5=vab::parse(view(source));
  flag5.samples[0].bytes.assign(16,0); flag5.samples[0].bytes[1]=5;
  rejects("one-packet one-shot LOOP START",[&]{vab::prepareTone(flag5,0);});
  auto sentinels=source; word(sentinels,0x840+22,255); word(sentinels,0xa20+22,0);
  auto bank=vab::parse(view(sentinels));
  CHECK(bank.tones[1].sample==255 && bank.tones[2].sample==0);
  checkPrepared(vab::prepareTone(bank,0),vab_fixture::kPrepared[0]);
  rejects("Noise sentinel", [&]{vab::prepareTone(bank,1);});
  rejects("unassigned sentinel", [&]{vab::prepareTone(bank,2);});
  rejects("out-of-range selection", [&]{vab::prepareTone(bank,3);});
  rejects("overflow-sized selection", [&]{vab::prepareTone(bank,std::numeric_limits<std::size_t>::max());});
  word(sentinels,0x16,0);
  for (const auto offset : {0x820,0x840,0xa20}) word(sentinels,offset+22,0);
  const auto unassigned=vab::parse(view(sentinels));
  CHECK(unassigned.samples.empty() && unassigned.tones.size()==3);
  rejects("all unassigned metadata is parseable", [&]{vab::prepareTone(unassigned,0);});
}

void testPublicBankCorruption(const Bytes& source) {
  using Mutation=std::pair<const char*,std::function<void(vab::Bank&)>>;
  const std::vector<Mutation> mutations{
    {"version", [](auto& b){b.version=8;}},
    {"empty programs", [](auto& b){b.programs.clear();}},
    {"program129", [](auto& b){b.programs.resize(129);}},
    {"empty tones", [](auto& b){b.tones.clear();}},
    {"too many tones", [](auto& b){b.tones.resize(33);}},
    {"sample256", [](auto& b){b.samples.resize(256);}},
    {"oversized owned sample", [](auto& b){b.samples[0].bytes.resize(vab::kMaxBodyBytes+8);}},
    {"oversized owned sample total", [](auto& b){b.samples[0].bytes.resize(vab::kMaxBodyBytes);}},
    {"sample identity", [](auto& b){b.samples[0].index=2;}},
    {"master midi", [](auto& b){b.masterPan=128;}},
    {"program midi", [](auto& b){b.programs[0].volume=128;}},
    {"program outside MIDI", [](auto& b){b.programs[0].index=128;}},
    {"program ordering", [](auto& b){std::swap(b.programs[0],b.programs[1]);}},
    {"duplicate program", [](auto& b){b.programs[1].index=b.programs[0].index;}},
    {"empty mapping", [](auto& b){b.programs[0].toneIndices.clear();}},
    {"too many mappings", [](auto& b){b.programs[0].toneIndices.resize(17);}},
    {"mapping out of bounds", [](auto& b){b.programs[0].toneIndices[0]=1000;}},
    {"mapping permutation", [](auto& b){std::swap(b.programs[0].toneIndices[0],b.programs[0].toneIndices[1]);}},
    {"missing mapping", [](auto& b){b.programs[0].toneIndices.pop_back();}},
    {"Tone Program identity", [](auto& b){b.tones[0].program=11;}},
    {"Tone index identity", [](auto& b){b.tones[0].index=1;}},
    {"tone MIDI", [](auto& b){b.tones[0].center=128;}},
    {"tone range", [](auto& b){b.tones[0].keyLow=100;}},
    {"tone sample", [](auto& b){b.tones[0].sample=3;}},
  };
  for (const auto& mutation : mutations) {
    auto bank=vab::parse(view(source)); mutation.second(bank);
    rejects(mutation.first,[&]{vab::prepareTone(bank,0);});
  }
}

std::pair<Bytes,Bytes> largeSample(const Bytes& vh,std::size_t sampleBytes) {
  auto header=vh;
  word(header,kSizeTable,0); word(header,kSizeTable+2,static_cast<unsigned>(sampleBytes/8));
  word(header,kSizeTable+4,2);
  Bytes body(sampleBytes+16,0);
  body[sampleBytes-15]=1; body[sampleBytes+1]=1;
  number(header,12,header.size()+body.size());
  return {std::move(header),std::move(body)};
}
void testRamAndFormatLimits(const Bytes& vh) {
  auto exact=largeSample(vh,520192);
  const auto prepared=vab::prepareTone(vab::parse(view(exact.first),view(exact.second)),0);
  CHECK(prepared.adpcm.size()==520192 && prepared.decodedFrames==910336);
  CHECK(!prepared.loopStartSamples && prepared.trimmedBytes==0);
  for (const auto length : {std::size_t(520208),std::size_t(524272)}) {
    auto fixture=largeSample(vh,length);
    const auto bank=vab::parse(view(fixture.first),view(fixture.second));
    CHECK(bank.samples[0].bytes.size()==length);
    rejects("parsed VB fits but selected ADPCM exceeds usable RAM",[&]{vab::prepareTone(bank,0);});
    CHECK(vab::prepareTone(bank,2).adpcm.size()==16);
    fixture.second[1]=1; // An early END makes the retained payload fit.
    const auto trimmed=vab::prepareTone(vab::parse(view(fixture.first),view(fixture.second)),0);
    CHECK(trimmed.adpcm.size()==16 && trimmed.trimmedBytes==length-16);
  }
}

void testMaximumCounts(const Bytes& source,const Bytes& vh,const Bytes& vb) {
  constexpr std::size_t sizeTable=0x820+128*0x200, headerBytes=sizeTable+0x200;
  Bytes maximum(headerBytes+16,0);
  std::copy_n(source.begin(),32,maximum.begin());
  number(maximum,12,maximum.size()); word(maximum,0x12,128);
  word(maximum,0x14,2048); word(maximum,0x16,1);
  for (unsigned program=0;program<128;++program) {
    const auto p=0x20+program*16;
    maximum[p]=16; maximum[p+1]=127; maximum[p+4]=64;
    for (unsigned tone=0;tone<16;++tone) {
      const auto at=0x820+program*0x200+tone*32;
      std::copy_n(source.begin()+0x820,32,maximum.begin()+at);
      word(maximum,at+20,program);
    }
  }
  word(maximum,sizeTable+2,2);
  std::copy_n(vb.begin()+96,16,maximum.begin()+headerBytes);
  const auto bank=vab::parse(view(maximum));
  CHECK(bank.programs.size()==128 && bank.tones.size()==2048 && bank.samples.size()==1);
  CHECK(bank.tones.back().program==127 && bank.tones.back().index==15);
  CHECK(vab::prepareTone(bank,2047).adpcm.size()==16);

  auto header=vh; word(header,0x16,255); word(header,kSizeTable,0);
  Bytes body(255*16,0);
  for (unsigned sample=1;sample<=255;++sample) {
    word(header,kSizeTable+2*sample,2);
    body[(sample-1)*16+1]=1;
  }
  number(header,12,header.size()+body.size());
  auto many=vab::parse(view(header),view(body));
  CHECK(many.samples.size()==255 && many.samples.back().index==255);
  CHECK(vab::prepareTone(many,0).adpcm.size()==16);
  many.tones[0].sample=255;
  rejects("Noise sentinel remains Noise even with 255 physical Samples",[&]{vab::prepareTone(many,0);});
}
}

int main() {
  try {
    const auto vab=fixture("spu-synth-demo.vab"), vh=fixture("demo.vh"), vb=fixture("demo.vb");
    testFixtureAndOwnership(vab,vh,vb);
    testJavaScriptPreparations(vab);
    testMalformedInputs(vab,vh,vb);
    testSelectedSampleValidation(vab);
    testPublicBankCorruption(vab);
    testRamAndFormatLimits(vh);
    testMaximumCounts(vab,vh,vb);
    std::printf("PASS VAB parser/preparation: %zu assertions, %zu rejected inputs, 7 JavaScript oracle cases; v6/v7, split/complete, ownership, ADPCM, sentinels, RAM and count limits\n",assertions,rejected);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr,"FAIL unexpected exception: %s\n",error.what());
    return 1;
  }
}
