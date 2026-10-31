// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "InstrumentState.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tetorica {
namespace {
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
void gain(double value) {
  if(!std::isfinite(value)||value<0||value>100)fail("SPU state: Gain must be finite 0-100 percent");
}
void envelope(const EnvelopeSettings& value) {
  // Custom parameters cover the documented fields only. The source Tone's
  // reserved bit13 is retained separately and merged by the DSP wrapper.
  if(value.custom.adsr2&0x2000)fail("SPU state: reserved custom ADSR2 bit13 must be zero");
}
void reverb(const ReverbSettings& value) {
  if(static_cast<std::uint32_t>(value.preset)>static_cast<std::uint32_t>(ReverbPreset::HalfEcho))
    fail("SPU state: unsupported reverb preset");
  for(double volume:{value.volumeLeft,value.volumeRight})
    if(!std::isfinite(volume)||volume<0||volume>1)
      fail("SPU state: reverb return must be finite 0-1");
}
void noise(const NoiseSettings& value) {
  if(value.clock>63)fail("SPU state: Noise clock must be 0-63");
}
void pmon(const PmonSettings& value) {
  if(static_cast<unsigned>(value.waveform)>static_cast<unsigned>(WaveformShape::Square))
    fail("SPU state: unsupported PMON waveform");
  if(!std::isfinite(value.ratio)||value.ratio<.01||value.ratio>16||
     !std::isfinite(value.depth)||value.depth<0||value.depth>1||
     !std::isfinite(value.decayMs)||value.decayMs<0||value.decayMs>5000)
    fail("SPU state: invalid PMON ratio, depth or decay");
  // Raw Noise+PMON enabled is valid authoring/automation state. The consumer
  // resolves its effective topology with Noise priority without changing it.
}
std::size_t selected(const vab::Bank& bank,std::uint32_t program,std::uint32_t tone) {
  if(program>127||tone>15)fail("SPU state: Program/Tone is out of range");
  for(std::size_t i=0;i<bank.tones.size();++i)
    if(bank.tones[i].program==program&&bank.tones[i].index==tone)return i;
  fail("SPU state: selected Program/Tone is absent from the bank");
}
std::uint32_t word(const std::uint8_t* bytes) {
  std::uint32_t value=0;for(unsigned i=0;i<4;++i)value|=std::uint32_t(bytes[i])<<(i*8);return value;
}
void putWord(std::uint8_t* bytes,std::uint32_t value) {
  for(unsigned i=0;i<4;++i)bytes[i]=static_cast<std::uint8_t>(value>>(i*8));
}
float readSample(const std::uint8_t* bytes) {
  static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,
                "SPU PCM state requires IEEE754 binary32");
  const auto bits=word(bytes);float value;std::memcpy(&value,&bits,sizeof value);return value;
}
void putSample(std::uint8_t* bytes,float value) {
  std::uint32_t bits;std::memcpy(&bits,&value,sizeof value);putWord(bytes,bits);
}
double readGain(const std::uint8_t* bytes) {
  static_assert(sizeof(double)==8&&std::numeric_limits<double>::is_iec559,
                "SPU state requires IEEE754 binary64");
  std::uint64_t bits=0;for(unsigned i=0;i<8;++i)bits|=std::uint64_t(bytes[i])<<(i*8);
  double value;std::memcpy(&value,&bits,sizeof value);return value;
}
void putGain(std::uint8_t* bytes,double value) {
  std::uint64_t bits;std::memcpy(&bits,&value,sizeof bits);
  for(unsigned i=0;i<8;++i)bytes[i]=static_cast<std::uint8_t>(bits>>(i*8));
}
}

InstrumentDefinition::InstrumentDefinition(Kind kind,std::vector<std::uint8_t> bank,
                                           std::uint32_t program,std::uint32_t tone,PcmCycle cycle)
  :mKind(kind),mBank(std::move(bank)),mProgram(program),mTone(tone),mPcmCycle(cycle) {}

std::shared_ptr<const InstrumentDefinition> InstrumentDefinition::builtin() {
  static const std::shared_ptr<const InstrumentDefinition> definition(
      new InstrumentDefinition(Kind::Builtin,{},0,0));
  return definition;
}

std::shared_ptr<const InstrumentDefinition> InstrumentDefinition::fromVab(
    std::vector<std::uint8_t> bytes,std::uint32_t program,std::uint32_t tone) {
  if(bytes.size()>vab::kMaxFileBytes)fail("SPU state: VAB exceeds 1 MiB");
  if(program>127||tone>15)fail("SPU state: Program/Tone is out of range");
  const auto bank=vab::parse({bytes.data(),bytes.size()});
  const auto prepared=vab::prepareTone(bank,selected(bank,program,tone));
  (void)prepared;
  // Even an rvalue vector may have caller-held raw aliases. Give the immutable
  // definition a fresh private allocation, not the transferred input storage.
  std::vector<std::uint8_t> owned(bytes);
  return std::shared_ptr<const InstrumentDefinition>(
      new InstrumentDefinition(Kind::Vab,std::move(owned),program,tone));
}

std::shared_ptr<const InstrumentDefinition> InstrumentDefinition::fromVabFlatIndex(
    std::vector<std::uint8_t> bytes,std::size_t flatIndex) {
  if(bytes.size()>vab::kMaxFileBytes)fail("SPU state: VAB exceeds 1 MiB");
  const auto bank=vab::parse({bytes.data(),bytes.size()});
  const auto prepared=vab::prepareTone(bank,flatIndex);
  std::vector<std::uint8_t> owned(bytes);
  return std::shared_ptr<const InstrumentDefinition>(new InstrumentDefinition(
      Kind::Vab,std::move(owned),prepared.tone.program,prepared.tone.index));
}

std::shared_ptr<const InstrumentDefinition> InstrumentDefinition::fromPcmCycle(const PcmCycle& cycle) {
  validatePcmCycle(cycle);
  return std::shared_ptr<const InstrumentDefinition>(new InstrumentDefinition(Kind::PcmCycle,{},0,0,cycle));
}

std::vector<std::uint8_t> encodeState(const InstrumentDefinition& definition,double gainPercent,
                                    EnvelopeSettings settings,ReverbSettings effects,NoiseSettings generator,PmonSettings modulation) {
  gain(gainPercent);
  envelope(settings);
  reverb(effects);
  noise(generator);
  pmon(modulation);
  const auto& bank=definition.bankBytes();
  if(bank.size()>vab::kMaxFileBytes)fail("SPU state: VAB exceeds 1 MiB");
  const bool pcm=definition.kind()==InstrumentDefinition::Kind::PcmCycle;
  const auto size=pcm?definition.pcmCycle().size()*4:bank.size();
  std::vector<std::uint8_t> bytes(kStateFixedBytes+size);
  std::copy(kStateMagic.begin(),kStateMagic.end(),bytes.begin());
  putWord(bytes.data()+8,kStateSchemaVersion);
  putWord(bytes.data()+12,static_cast<std::uint32_t>(bytes.size()));
  putWord(bytes.data()+16,static_cast<std::uint32_t>(definition.kind()));
  putWord(bytes.data()+20,definition.program());putWord(bytes.data()+24,definition.tone());
  putWord(bytes.data()+28,static_cast<std::uint32_t>(size));
  if(pcm)for(std::size_t i=0;i<definition.pcmCycle().size();++i)
    putSample(bytes.data()+kStateHeaderBytes+i*4,definition.pcmCycle()[i]);
  else std::copy(bank.begin(),bank.end(),bytes.begin()+kStateHeaderBytes);
  putGain(bytes.data()+kStateHeaderBytes+size,gainPercent);
  putWord(bytes.data()+40+size,std::uint32_t(settings.custom.adsr1)|
    (std::uint32_t(settings.custom.adsr2)<<16));
  putWord(bytes.data()+44+size,(settings.enabled?1u:0u)|(settings.live?2u:0u));
  putWord(bytes.data()+48+size,static_cast<std::uint32_t>(effects.preset));
  putWord(bytes.data()+52+size,(effects.enabled?1u:0u)|(effects.useToneSend?2u:0u));
  putGain(bytes.data()+56+size,effects.volumeLeft);
  putGain(bytes.data()+64+size,effects.volumeRight);
  putWord(bytes.data()+72+size,generator.enabled?1u:0u);
  putWord(bytes.data()+76+size,generator.clock);
  putWord(bytes.data()+80+size,modulation.enabled?1u:0u);
  putWord(bytes.data()+84+size,static_cast<std::uint32_t>(modulation.waveform));
  putGain(bytes.data()+88+size,modulation.ratio);
  putGain(bytes.data()+96+size,modulation.depth);
  putGain(bytes.data()+104+size,modulation.decayMs);
  return bytes;
}

std::vector<std::uint8_t> encodeState(
    const std::shared_ptr<const InstrumentDefinition>& definition,double gainPercent,
    EnvelopeSettings settings,ReverbSettings effects,NoiseSettings generator,PmonSettings modulation) {
  if(!definition)throw std::invalid_argument("SPU state: missing instrument definition");
  return encodeState(*definition,gainPercent,settings,effects,generator,modulation);
}

SavedInstrumentState decodeState(vab::BytesView bytes) {
  if(!bytes.data&&bytes.size)throw std::invalid_argument("SPU state: null input");
  if(bytes.size<40||bytes.size>kMaxStateBytes)fail("SPU state: invalid total length");
  if(std::memcmp(bytes.data,kStateMagic.data(),kStateMagic.size()))fail("SPU state: invalid signature");
  const auto version=word(bytes.data+8);
  if(version<1||version>kStateSchemaVersion)fail("SPU state: unsupported schema");
  if(word(bytes.data+12)!=bytes.size)fail("SPU state: declared length differs");
  const auto kind=word(bytes.data+16),program=word(bytes.data+20),tone=word(bytes.data+24),n=word(bytes.data+28);
  const auto fixed=version==1?40:version==2?48:version<=4?72:version==5?80:kStateFixedBytes;
  if(n>vab::kMaxFileBytes||std::uint64_t(n)+fixed!=bytes.size)fail("SPU state: invalid bank length");
  if(kind>(version>=4?2u:1u))fail("SPU state: unsupported instrument kind");
  if(kind==0&&(n||program||tone))fail("SPU state: builtin must not contain a bank or selection");
  if(kind==1&&(!n||program>127||tone>15))fail("SPU state: invalid VAB selection");
  if(kind==2&&(n!=sizeof(float)*PcmCycle{}.size()||program||tone))fail("SPU state: invalid PCM cycle length or selection");
  const auto gainPercent=readGain(bytes.data+32+n);gain(gainPercent);
  EnvelopeSettings settings;
  if(version>=2) {
    const auto packed=word(bytes.data+40+n),flags=word(bytes.data+44+n);
    if(flags&~3u)fail("SPU state: unknown envelope flags");
    settings.custom={static_cast<std::uint16_t>(packed),static_cast<std::uint16_t>(packed>>16)};
    settings.enabled=(flags&1)!=0;settings.live=(flags&2)!=0;
    envelope(settings);
  }
  ReverbSettings effects;
  if(version>=3) {
    const auto preset=word(bytes.data+48+n),flags=word(bytes.data+52+n);
    if(flags&~3u)fail("SPU state: unknown reverb flags");
    effects.preset=static_cast<ReverbPreset>(preset);
    effects.enabled=(flags&1)!=0;effects.useToneSend=(flags&2)!=0;
    effects.volumeLeft=readGain(bytes.data+56+n);effects.volumeRight=readGain(bytes.data+64+n);
    reverb(effects);
  }
  NoiseSettings generator;
  if(version>=5) {
    const auto flags=word(bytes.data+72+n);
    if(flags&~1u)fail("SPU state: unknown Noise flags");
    generator.enabled=(flags&1)!=0;generator.clock=word(bytes.data+76+n);
    noise(generator);
  }
  PmonSettings modulation;
  if(version>=6) {
    const auto flags=word(bytes.data+80+n);
    if(flags&~1u)fail("SPU state: unknown PMON flags");
    modulation.enabled=(flags&1)!=0;modulation.waveform=static_cast<WaveformShape>(word(bytes.data+84+n));
    modulation.ratio=readGain(bytes.data+88+n);modulation.depth=readGain(bytes.data+96+n);
    modulation.decayMs=readGain(bytes.data+104+n);pmon(modulation);
  }
  if(kind==0)return {InstrumentDefinition::builtin(),gainPercent,settings,effects,generator,modulation};
  if(kind==2) {
    PcmCycle cycle{};
    for(std::size_t i=0;i<cycle.size();++i)cycle[i]=readSample(bytes.data+kStateHeaderBytes+i*4);
    return {InstrumentDefinition::fromPcmCycle(cycle),gainPercent,settings,effects,generator,modulation};
  }
  std::vector<std::uint8_t> bank(bytes.data+kStateHeaderBytes,bytes.data+kStateHeaderBytes+n);
  return {InstrumentDefinition::fromVab(std::move(bank),program,tone),gainPercent,settings,effects,generator,modulation};
}

} // namespace tetorica
