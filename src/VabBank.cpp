// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Deliberate C++ port of Tetorica's docs/playground/playground_ps1_vab.js
// and preparePs1ADPCM in docs/js/ps1synth.js (parent hello_ymfm workspace).
// Layout references retained from that BSD-3-Clause implementation:
// https://github.com/vgmtrans/vgmtrans/blob/master/src/main/formats/PS1/Vab.cpp
// https://psx-spx.consoledev.net/cdromfileformats/#vab-header-vh
// https://github.com/Xeeynamo/sotn-decomp/blob/master/src/main/psxsdk/libsnd/vmanager.c
// https://github.com/Xeeynamo/sotn-decomp/blob/master/src/main/psxsdk/libsnd/vs_vh.c
#include "VabBank.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace tetorica::vab {
namespace {
constexpr std::size_t kProgramTable = 0x20;
constexpr std::size_t kToneTables = 0x820;
constexpr std::size_t kToneTableSize = 0x200;
constexpr std::size_t kSizeTableSize = 0x200;

[[noreturn]] void fail(const std::string& message)
{
  throw std::runtime_error("VAB: " + message);
}

void checkView(BytesView bytes, const char* name, std::size_t maximum)
{
  if (!bytes.data && bytes.size)
    throw std::invalid_argument(std::string("VAB: null ") + name + " bytes");
  if (bytes.size > maximum)
    fail(std::string(name) + " exceeds its input size limit");
}

void range(BytesView bytes, std::size_t at, std::size_t size)
{
  if (at > bytes.size || size > bytes.size - at)
    fail("truncated table or field");
}

std::uint16_t u16(BytesView bytes, std::size_t at)
{
  range(bytes, at, 2);
  return static_cast<std::uint16_t>(bytes.data[at]) |
    static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes.data[at + 1]) << 8);
}

std::uint32_t u32(BytesView bytes, std::size_t at)
{
  range(bytes, at, 4);
  return static_cast<std::uint32_t>(bytes.data[at]) |
    (static_cast<std::uint32_t>(bytes.data[at + 1]) << 8) |
    (static_cast<std::uint32_t>(bytes.data[at + 2]) << 16) |
    (static_cast<std::uint32_t>(bytes.data[at + 3]) << 24);
}

std::uint8_t midi(std::uint8_t value, const char* name)
{
  if (value > 127)
    fail(std::string(name) + " must be 0-127");
  return value;
}

void validateToneMetadata(const Tone& tone, std::size_t sampleCount)
{
  if (tone.program > 127 || tone.index > 15)
    fail("invalid Program or Tone identity");
  midi(tone.volume, "tone volume");
  midi(tone.pan, "tone pan");
  midi(tone.center, "center note");
  midi(tone.shift, "fine tuning");
  midi(tone.keyLow, "key minimum");
  midi(tone.keyHigh, "key maximum");
  if (tone.keyLow > tone.keyHigh ||
      (tone.sample > sampleCount && tone.sample != 255))
    fail("invalid key range or sample reference");
  // Mode, vibrato/portamento bytes and bend ranges remain raw metadata, as in
  // the original parser. This selected-Tone importer does not execute a driver.
}

Bank parseImpl(BytesView header, BytesView explicitBody, bool split)
{
  checkView(header, split ? "VH header" : "complete file", kMaxFileBytes);
  if (split)
    checkView(explicitBody, "VB sample body", kMaxBodyBytes);
  if (header.size < kToneTables || header.data[0] != 'p' || header.data[1] != 'B' ||
      header.data[2] != 'A' || header.data[3] != 'V')
    fail("expected a standard pBAV header");

  const auto version = u32(header, 4);
  const std::size_t programCount = u16(header, 0x12);
  const std::size_t toneCount = u16(header, 0x14);
  const std::size_t sampleCount = u16(header, 0x16);
  if (version != 6 && version != 7)
    fail("unsupported version (initial support: 6 and 7)");
  if (!programCount || programCount > 128 || !toneCount ||
      toneCount > programCount * 16 || sampleCount > 255)
    fail("invalid program, tone or sample count");
  const auto masterVolume = midi(header.data[0x18], "master volume");
  const auto masterPan = midi(header.data[0x19], "master pan");
  // Counts are bounded above before these additions/multiplications.
  const std::size_t sizeTable = kToneTables + programCount * kToneTableSize;
  const std::size_t bodyOffset = sizeTable + kSizeTableSize;
  range(header, 0, bodyOffset);
  const BytesView body = split ? explicitBody :
    BytesView{header.data + bodyOffset, header.size - bodyOffset};
  checkView(body, "VB sample body", kMaxBodyBytes);
  if ((split && header.size != bodyOffset) || u32(header, 0x0c) != bodyOffset + body.size)
    fail("declared file size does not match VH + VB (choose the matching pair)");

  struct Slice { std::size_t offset = 0, size = 0; };
  std::array<Slice, 255> slices{};
  // Size entry zero reserves leading VB bytes; it is not Sample #1.
  std::size_t offset = static_cast<std::size_t>(u16(header, sizeTable)) * 8;
  if (offset > body.size)
    fail("sample body prefix exceeds VB size");
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
  {
    const std::size_t size = static_cast<std::size_t>(u16(header, sizeTable + (sample + 1) * 2)) * 8;
    if (size < 16 || size > body.size - offset)
      fail("invalid sample size or boundary");
    slices[sample] = {offset, size};
    offset += size;
  }

  Bank bank;
  bank.version = version;
  bank.id = u32(header, 8);
  bank.masterVolume = masterVolume;
  bank.masterPan = masterPan;
  bank.programs.reserve(programCount);
  bank.tones.reserve(toneCount);
  std::size_t packed = 0;
  for (std::size_t index = 0; index < 128; ++index)
  {
    const std::size_t at = kProgramTable + index * 16;
    const std::size_t count = header.data[at];
    if (!count)
      continue;
    if (count > 16 || packed >= programCount || count > toneCount - bank.tones.size())
      fail("invalid tone count in program table");
    Program program;
    program.index = static_cast<std::uint16_t>(index);
    program.volume = midi(header.data[at + 1], "program volume");
    program.pan = midi(header.data[at + 4], "program pan");
    program.toneIndices.reserve(count);
    for (std::size_t toneIndex = 0; toneIndex < count; ++toneIndex)
    {
      const std::size_t t = kToneTables + packed * kToneTableSize + toneIndex * 32;
      range(header, t, 32);
      Tone tone;
      tone.program = static_cast<std::uint16_t>(index);
      tone.index = static_cast<std::uint16_t>(toneIndex);
      tone.sample = u16(header, t + 22);
      tone.mode = header.data[t + 1];
      tone.volume = header.data[t + 2];
      tone.pan = header.data[t + 3];
      tone.center = header.data[t + 4];
      tone.shift = header.data[t + 5];
      tone.keyLow = header.data[t + 6];
      tone.keyHigh = header.data[t + 7];
      tone.adsr1 = u16(header, t + 16);
      tone.adsr2 = u16(header, t + 18);
      tone.vibratoWidth = header.data[t + 8];
      tone.vibratoTime = header.data[t + 9];
      tone.portamentoWidth = header.data[t + 10];
      tone.portamentoTime = header.data[t + 11];
      tone.bendMin = header.data[t + 12];
      tone.bendMax = header.data[t + 13];
      validateToneMetadata(tone, sampleCount);
      program.toneIndices.push_back(bank.tones.size());
      bank.tones.push_back(tone);
    }
    ++packed;
    bank.programs.push_back(std::move(program));
  }
  if (packed != programCount || bank.tones.size() != toneCount)
    fail("program/tone counts disagree with the tables");

  // Defer all sample payload copies until the complete structural/metadata pass
  // has succeeded. Unreferenced trailing body padding is deliberately ignored.
  bank.samples.reserve(sampleCount);
  for (std::size_t sample = 0; sample < sampleCount; ++sample)
  {
    const auto slice = slices[sample];
    Sample entry;
    entry.index = static_cast<std::uint16_t>(sample + 1);
    entry.bytes.assign(body.data + slice.offset, body.data + slice.offset + slice.size);
    bank.samples.push_back(std::move(entry));
  }
  return bank;
}

void validateBank(const Bank& bank)
{
  if ((bank.version != 6 && bank.version != 7) || bank.programs.empty() ||
      bank.programs.size() > 128 || bank.tones.empty() ||
      bank.tones.size() > bank.programs.size() * 16 || bank.samples.size() > 255)
    fail("invalid bank metadata or counts");
  midi(bank.masterVolume, "master volume");
  midi(bank.masterPan, "master pan");
  std::size_t sampleBytes = 0;
  for (std::size_t index = 0; index < bank.samples.size(); ++index)
  {
    const auto& sample = bank.samples[index];
    if (sample.index != index + 1 || sample.bytes.size() < 16 || sample.bytes.size() % 8 ||
        sample.bytes.size() > kMaxBodyBytes - sampleBytes)
      fail("invalid owned sample size, total size or identity");
    sampleBytes += sample.bytes.size();
  }
  std::size_t flat = 0;
  int previousProgram = -1;
  for (const auto& program : bank.programs)
  {
    if (program.index > 127 || static_cast<int>(program.index) <= previousProgram ||
        program.toneIndices.empty() || program.toneIndices.size() > 16 ||
        program.toneIndices.size() > bank.tones.size() - flat)
      fail("invalid owned Program identity or Tone count");
    midi(program.volume, "program volume");
    midi(program.pan, "program pan");
    for (std::size_t index = 0; index < program.toneIndices.size(); ++index, ++flat)
    {
      if (program.toneIndices[index] != flat)
        fail("invalid owned Program/Tone mapping");
      const auto& tone = bank.tones[flat];
      if (tone.program != program.index || tone.index != index)
        fail("owned Tone identity disagrees with its Program");
      validateToneMetadata(tone, bank.samples.size());
    }
    previousProgram = program.index;
  }
  if (flat != bank.tones.size())
    fail("owned Program/Tone counts disagree");
}
} // namespace

Bank parse(BytesView complete)
{
  return parseImpl(complete, {}, false);
}

Bank parse(BytesView header, BytesView body)
{
  return parseImpl(header, body, true);
}

PreparedTone prepareTone(const Bank& bank, std::size_t flatIndex)
{
  validateBank(bank);
  if (flatIndex >= bank.tones.size())
    fail("choose a tone");
  const auto& tone = bank.tones[flatIndex];
  if (!tone.sample)
    fail("this Tone has no assigned sample");
  if (tone.sample == 255)
    fail("libsnd Noise tone (sample 255) is not supported by this importer yet");
  const auto& bytes = bank.samples[tone.sample - 1].bytes;

  std::size_t end = 0;
  std::optional<std::uint32_t> marker;
  for (std::size_t at = 0; at <= bytes.size() - 16; at += 16)
  {
    const auto header = bytes[at];
    const auto flags = bytes[at + 1];
    if ((header >> 4) > 4 || (header & 15) > 12)
      fail("SPU ADPCM predictor must be 0-4 and shift 0-12");
    if (flags & ~7)
      fail("unsupported SPU ADPCM packet flags");
    if (flags & 4)
    {
      if (marker)
        fail("multiple loop-start markers are not supported yet");
      marker = static_cast<std::uint32_t>((at / 16) * 28);
    }
    if (flags & 1)
    {
      end = at + 16;
      break;
    }
  }
  if (!end)
    fail("selected sample has no END packet");
  if (end > kMaxSampleBytes)
    fail("SPU ADPCM exceeds available sample RAM");
  const bool loop = (bytes[end - 15] & 2) != 0;
  if (!loop && marker)
    fail("one-shot sample contains a loop-start marker; unsupported flag pattern");

  PreparedTone result;
  result.rootFrequencyHz = 440. * std::exp2((tone.center - tone.shift / 128. - 69.) / 12.);
  if (!std::isfinite(result.rootFrequencyHz) || result.rootFrequencyHz <= 0)
    fail("invalid selected Tone tuning");
  result.decodedFrames = static_cast<std::uint32_t>((end / 16) * 28);
  if (loop)
    result.loopStartSamples = marker.value_or(0);

  const Program* program = nullptr;
  for (const auto& candidate : bank.programs)
    if (candidate.index == tone.program)
    {
      program = &candidate;
      break;
    }
  // validateBank guarantees the mapping; retain this guard as local protection.
  if (!program)
    fail("selected Tone has no matching Program");
  const double level = (bank.masterVolume / 127.) * (program->volume / 127.) * (tone.volume / 127.);
  double left = 1, right = 1;
  for (const auto pan : {bank.masterPan, program->pan, tone.pan})
  {
    left *= pan < 64 ? 1. : (127. - pan) / 63.;
    right *= pan < 64 ? pan / 63. : 1.;
  }
  result.volumeLeft = (level * left) * (level * left);
  result.volumeRight = (level * right) * (level * right);
  result.adsr1 = tone.adsr1;
  result.adsr2 = tone.adsr2;
  result.reverb = (tone.mode & 4) != 0;
  result.trimmedBytes = bytes.size() - end;
  result.tone = tone;
  result.adpcm.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(end));
  return result;
}

} // namespace tetorica::vab
