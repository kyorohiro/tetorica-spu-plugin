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
                                           std::uint32_t program,std::uint32_t tone)
  :mKind(kind),mBank(std::move(bank)),mProgram(program),mTone(tone) {}

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

std::vector<std::uint8_t> encodeState(const InstrumentDefinition& definition,double gainPercent) {
  gain(gainPercent);
  const auto& bank=definition.bankBytes();
  if(bank.size()>vab::kMaxFileBytes)fail("SPU state: VAB exceeds 1 MiB");
  std::vector<std::uint8_t> bytes(40+bank.size());
  std::copy(kStateMagic.begin(),kStateMagic.end(),bytes.begin());
  putWord(bytes.data()+8,kStateSchemaVersion);
  putWord(bytes.data()+12,static_cast<std::uint32_t>(bytes.size()));
  putWord(bytes.data()+16,static_cast<std::uint32_t>(definition.kind()));
  putWord(bytes.data()+20,definition.program());putWord(bytes.data()+24,definition.tone());
  putWord(bytes.data()+28,static_cast<std::uint32_t>(bank.size()));
  std::copy(bank.begin(),bank.end(),bytes.begin()+kStateHeaderBytes);
  putGain(bytes.data()+kStateHeaderBytes+bank.size(),gainPercent);
  return bytes;
}

std::vector<std::uint8_t> encodeState(
    const std::shared_ptr<const InstrumentDefinition>& definition,double gainPercent) {
  if(!definition)throw std::invalid_argument("SPU state: missing instrument definition");
  return encodeState(*definition,gainPercent);
}

SavedInstrumentState decodeState(vab::BytesView bytes) {
  if(!bytes.data&&bytes.size)throw std::invalid_argument("SPU state: null input");
  if(bytes.size<40||bytes.size>kMaxStateBytes)fail("SPU state: invalid total length");
  if(std::memcmp(bytes.data,kStateMagic.data(),kStateMagic.size()))fail("SPU state: invalid signature");
  if(word(bytes.data+8)!=kStateSchemaVersion)fail("SPU state: unsupported schema");
  if(word(bytes.data+12)!=bytes.size)fail("SPU state: declared length differs");
  const auto kind=word(bytes.data+16),program=word(bytes.data+20),tone=word(bytes.data+24),n=word(bytes.data+28);
  if(n>vab::kMaxFileBytes||std::uint64_t(n)+40!=bytes.size)fail("SPU state: invalid bank length");
  if(kind>1)fail("SPU state: unsupported instrument kind");
  if(kind==0&&(n||program||tone))fail("SPU state: builtin must not contain a bank or selection");
  if(kind==1&&(!n||program>127||tone>15))fail("SPU state: invalid VAB selection");
  const auto gainPercent=readGain(bytes.data+bytes.size-8);gain(gainPercent);
  if(kind==0)return {InstrumentDefinition::builtin(),gainPercent};
  std::vector<std::uint8_t> bank(bytes.data+kStateHeaderBytes,bytes.data+kStateHeaderBytes+n);
  return {InstrumentDefinition::fromVab(std::move(bank),program,tone),gainPercent};
}

} // namespace tetorica
