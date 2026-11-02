// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "PsfImport.h"
#include "VabBank.h"
#include "zlib.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef PSF_FIXTURE_DIR
#define PSF_FIXTURE_DIR "tests/fixtures/psf"
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace psf = tetorica::psf;
namespace vab = tetorica::vab;
using Bytes = std::vector<std::uint8_t>;
namespace {
Bytes fixture(const char* name)
{
  std::ifstream input(std::string(PSF_FIXTURE_DIR) + "/" + name, std::ios::binary);
  CHECK(input.good());
  return Bytes(std::istreambuf_iterator<char>(input), {});
}
void w32(Bytes& bytes, std::size_t at, std::uint32_t value)
{ for (unsigned n = 0; n < 4; ++n) bytes.at(at + n) = (value >> (8 * n)) & 255; }
std::uint32_t r32(const Bytes& bytes, std::size_t at)
{ return std::uint32_t(bytes.at(at)) | (std::uint32_t(bytes.at(at + 1)) << 8) |
    (std::uint32_t(bytes.at(at + 2)) << 16) | (std::uint32_t(bytes.at(at + 3)) << 24); }
void magic(Bytes& bytes, std::size_t at, const std::string& text)
{ std::copy(text.begin(), text.end(), bytes.begin() + at); }
Bytes fromExe(const Bytes& exe, const std::string& tags = {}, const Bytes& reserved = {})
{
  Bytes compressed(compressBound(static_cast<uLong>(exe.size())));
  uLongf size = compressed.size();
  CHECK(compress2(compressed.data(), &size, exe.data(), static_cast<uLong>(exe.size()), Z_BEST_SPEED) == Z_OK);
  compressed.resize(size);
  Bytes bytes(16 + reserved.size() + size);
  magic(bytes, 0, "PSF"); bytes[3] = 1;
  w32(bytes, 4, static_cast<std::uint32_t>(reserved.size()));
  w32(bytes, 8, static_cast<std::uint32_t>(size));
  w32(bytes, 12, crc32(0L, compressed.data(), static_cast<uInt>(size)));
  std::copy(reserved.begin(), reserved.end(), bytes.begin() + 16);
  std::copy(compressed.begin(), compressed.end(), bytes.begin() + 16 + reserved.size());
  if (!tags.empty()) { const auto suffix = "[TAG]" + tags; bytes.insert(bytes.end(), suffix.begin(), suffix.end()); }
  return bytes;
}
Bytes make(const Bytes& data = {}, std::uint32_t address = 0x80010000,
           const std::string& tags = {}, const Bytes& reserved = {})
{
  Bytes exe(0x800 + data.size()); magic(exe, 0, "PS-X EXE");
  w32(exe, 0x18, address); w32(exe, 0x1c, static_cast<std::uint32_t>(data.size()));
  std::copy(data.begin(), data.end(), exe.begin() + 0x800);
  return fromExe(exe, tags, reserved);
}
void rejects(const char* name, const std::string& reason, const std::function<void()>& action)
{
  try { action(); }
  catch (const std::runtime_error& error) {
    if (std::string(error.what()).find(reason) != std::string::npos) return;
    std::fprintf(stderr, "FAIL %s: expected %s, got %s\n", name, reason.c_str(), error.what()); std::exit(1);
  }
  std::fprintf(stderr, "FAIL %s: did not reject\n", name); std::exit(1);
}
psf::Result extract(const Bytes& bytes)
{ return psf::extract({{"a.psf", bytes}}, "a.psf"); }
vab::Bank parsed(const psf::Bank& bank)
{ return vab::parse({bank.bytes.data(), bank.bytes.size()}); }

void originalFixtureParity()
{
  const auto standalone = fixture("standalone.psf"), original = fixture("original.vab");
  auto result = extract(standalone);
  CHECK(result.banks.size() == 2); CHECK(result.candidates == 2); CHECK(result.diagnostics.empty());
  CHECK(result.banks[0].address == 0x10000 && result.banks[1].address == 0x20000);
  CHECK(result.banks[0].bytes == original);
  CHECK(parsed(result.banks[0]).id == 23 && parsed(result.banks[1]).id == 24);
  CHECK(result.entryPath == "a.psf" && result.loadedFiles == std::vector<std::string>{"a.psf"});
  CHECK(result.expandedBytes == 0x800 + 0x10000 + original.size());
  const auto tone = vab::prepareTone(parsed(result.banks[0]), 1);
  CHECK(tone.adpcm.size() == 64 && tone.tone.shift == 64);
  CHECK(vab::prepareTone(parsed(result.banks[0]), 2).adpcm.size() == 16);
  auto second = original; w32(second, 8, 24); CHECK(result.banks[1].bytes == second);

  const std::vector<psf::File> files{
    {"Demo\\track.minipsf", fixture("track.minipsf")},
    {"Demo/lib/Bank.psflib", fixture("Bank.psflib")},
    {"Demo/patch.psflib", fixture("patch.psflib")}};
  CHECK(psf::entries(files) == std::vector<std::string>{"Demo/track.minipsf"});
  result = psf::extract(files, "Demo/track.minipsf");
  CHECK(result.loadedFiles == (std::vector<std::string>{"Demo/lib/Bank.psflib", "Demo/track.minipsf", "Demo/patch.psflib"}));
  CHECK(result.banks.size() == 2 && result.banks[0].bytes == original);
  CHECK(vab::prepareTone(parsed(result.banks[0]), 1).tone.shift == 64);
  // _lib3 is ignored when _lib2 is absent; own text wins over primary library.
  result = psf::extract({{"a.psf", make({0}, 0x80010845, "_lib=Bank.psflib\n_lib3=missing.psflib")},
    {"Bank.psflib", fixture("Bank.psflib")}}, "a.psf");
  CHECK(result.banks.size() == 2 && parsed(result.banks[0]).tones[1].shift == 0);
  // Empty compressed sections represent no overlay and need not be EXE files.
  auto noProgram = Bytes(16); magic(noProgram, 0, "PSF"); noProgram[3] = 1;
  CHECK(extract(noProgram).banks.empty());
  CHECK(extract(fromExe({})).banks.empty());
}
void pathsAndDependencies()
{
  CHECK(psf::entries({{"bank.PSFLIB", make()}, {"other.psf1lib", make()}}) ==
    (std::vector<std::string>{"bank.PSFLIB", "other.psf1lib"}));
  CHECK(psf::entries({{"a/./b/../song.psf", make()}}) == std::vector<std::string>{"a/song.psf"});
  rejects("missing entry", "choose a PSF entry", [] { psf::extract({{"x.psf", make()}}, "y.psf"); });
  rejects("duplicate normalized path", "duplicate path", [] { psf::entries({{"./a.psf", make()}, {"a.psf", make()}}); });
  for (const auto& path : {"/a.psf", "\\a.psf", "C:a.psf", "../a.psf", "a/../../b.psf", ".", ""})
    rejects("unsafe selected path", "PSF:", [&] { psf::entries({{path, make()}}); });
  rejects("path length", "relative", [] { psf::entries({{std::string(1025, 'a'), make()}}); });
  rejects("control path", "relative", [] { psf::entries({{std::string("a\0.psf", 6), make()}}); });
  rejects("missing library", "missing PSFLib", [] { extract(make({}, 0, "_lib=missing.psflib")); });
  rejects("empty library", "relative", [] { extract(make({}, 0, "_lib=")); });
  rejects("escaping library", "escapes", [] { extract(make({}, 0, "_lib=../escape.psflib")); });
  rejects("duplicate lib tag", "duplicate _lib", [] { extract(make({}, 0, "_lib=a\n_LIB=b")); });
  rejects("duplicate numbered tag", "duplicate _lib2", [] { extract(make({}, 0, "_lib2=a\n_lib2=b")); });
  rejects("ambiguous fallback", "ambiguous", [] {
    psf::extract({{"a.psf", make({}, 0, "_lib=bank.psflib")},
      {"Bank.psflib", make()}, {"BANK.psflib", make()}}, "a.psf");
  });
  // Exact path wins even when a differently cased sibling exists.
  auto result = psf::extract({{"a.psf", make({}, 0, "_lib=Bank.psflib")},
    {"Bank.psflib", make()}, {"BANK.psflib", make()}}, "a.psf");
  CHECK(result.loadedFiles[0] == "Bank.psflib");
  result = psf::extract({{"Set/song.psf", make({}, 0, "_LIB=lib\\Bank.psflib")},
    {"Set/lib/BANK.PSFLIB", make({}, 0, "_lib=../common.psflib")},
    {"Set/common.psflib", make()}}, "Set/song.psf");
  CHECK(result.loadedFiles == (std::vector<std::string>{"Set/common.psflib", "Set/lib/BANK.PSFLIB", "Set/song.psf"}));
  rejects("cycle", "cyclic", [] {
    psf::extract({{"a.psf", make({}, 0, "_lib=b.psflib")},
      {"b.psflib", make({}, 0, "_lib=a.psf")}}, "a.psf");
  });
  std::vector<psf::File> chain;
  for (unsigned i = 0; i < 11; ++i) chain.push_back({std::to_string(i) + ".psf",
    make({}, 0, i < 10 ? "_lib=" + std::to_string(i + 1) + ".psf" : "")});
  rejects("depth bound", "depth/visit", [&] { psf::extract(chain, "0.psf"); });
  chain[9].bytes = make(); chain.resize(10);
  CHECK(psf::extract(chain, "0.psf").loadedFiles.size() == 10);
  std::string tags;
  for (unsigned n = 2; n <= 257; ++n) tags += "_lib" + std::to_string(n) + "=common.psflib\n";
  rejects("visit bound", "depth/visit", [&] {
    psf::extract({{"a.psf", make({}, 0, tags)}, {"common.psflib", make()}}, "a.psf");
  });
  // Reusing a dependency overlays again but counts its inflation only once.
  result = psf::extract({{"a.psf", make({}, 0, "_lib=common.psflib\n_lib2=common.psflib")},
    {"common.psflib", make()}}, "a.psf");
  CHECK(result.loadedFiles.size() == 3 && result.expandedBytes == 2 * 0x800);
}
void malformedAndBounds()
{
  auto valid = make({1});
  auto mutation = valid; mutation[3] = 2;
  rejects("PSF2", "PSF2", [&] { extract(mutation); });
  mutation = valid; mutation[0] = 0;
  rejects("signature", "expected a PSF", [&] { extract(mutation); });
  mutation = valid; mutation[16] ^= 1;
  rejects("compressed CRC", "CRC-32", [&] { extract(mutation); });
  mutation = valid; mutation.pop_back();
  rejects("truncated compressed", "truncated", [&] { extract(mutation); });
  mutation = valid; w32(mutation, 4, 0xffffffff);
  rejects("overflow reserved", "truncated", [&] { extract(mutation); });
  mutation = valid; w32(mutation, 8, 0xffffffff);
  rejects("overflow compressed", "truncated", [&] { extract(mutation); });
  CHECK(extract(make({1}, 0, "title=ok", {1, 2, 3})).banks.empty());
  // Recomputed compressed CRC must not bypass the inner zlib checksum.
  mutation = valid; mutation.back() ^= 1;
  w32(mutation, 12, crc32(0L, mutation.data() + 16, static_cast<uInt>(mutation.size() - 16)));
  rejects("zlib checksum", "zlib", [&] { extract(mutation); });
  mutation = valid; mutation.push_back(0); w32(mutation, 8, r32(mutation, 8) + 1);
  w32(mutation, 12, crc32(0L, mutation.data() + 16, static_cast<uInt>(mutation.size() - 16)));
  rejects("zlib trailing data", "zlib", [&] { extract(mutation); });
  rejects("NUL tags", "NUL", [] { extract(make({}, 0, std::string("title=a\0b", 9))); });
  rejects("large tags", "50,000", [] { extract(make({}, 0, std::string(50001, 'a'))); });
  rejects("no selected files", "1-512", [] { psf::entries({}); });
  rejects("many selected files", "1-512", [&] { psf::entries(std::vector<psf::File>(513, {"a.psf", valid})); });
  rejects("short file", "16 bytes", [] { extract(Bytes(15)); });
  rejects("oversized file", "4 MiB", [] { extract(Bytes(psf::kMaxFileBytes + 1)); });
  auto large = valid; large.resize(psf::kMaxFileBytes);
  std::vector<psf::File> largeSet;
  for (unsigned n = 0; n < 16; ++n) largeSet.push_back({std::to_string(n) + ".psf", large});
  CHECK(psf::entries(largeSet).size() == 16);
  largeSet.push_back({"over.psf", valid});
  rejects("set size", "64 MiB", [&] { psf::entries(largeSet); });
  rejects("EXE signature", "not a PS-X EXE", [] { extract(fromExe(Bytes(0x800))); });
  Bytes badText(0x800); magic(badText, 0, "PS-X EXE"); w32(badText, 0x1c, 1);
  rejects("EXE size", "truncated PS-X EXE", [&] { extract(fromExe(badText)); });
  rejects("expansion bomb", "expanded PS-X EXE", [] { extract(make(Bytes(2033664))); });
  std::vector<psf::File> expanded;
  std::string tags;
  const auto compressedLarge = make(Bytes(1970000), 0);
  for (unsigned n = 2; n <= 19; ++n) {
    const auto name = std::to_string(n) + ".psflib";
    tags += "_lib" + std::to_string(n) + '=' + name + '\n';
    expanded.push_back({name, compressedLarge});
  }
  expanded.push_back({"entry.psf", make({}, 0, tags)});
  rejects("total expanded programs", "32 MiB", [&] { psf::extract(expanded, "entry.psf"); });
}
void aliasesAndCoverage()
{
  const auto original = fixture("original.vab");
  for (const auto address : {0x00010000u, 0x00210000u, 0x80010000u, 0x80210000u, 0xa0010000u, 0xa0610000u}) {
    const auto result = extract(make(original, address));
    CHECK(result.banks.size() == 1 && result.banks[0].address == 0x10000 && result.banks[0].bytes == original);
  }
  for (const auto address : {0x00800000u, 0x80800000u, 0xa0800000u, 0xbf801000u, 0x40010000u})
    rejects("outside RAM", "outside PS1 RAM", [&] { extract(make({1}, address)); });
  rejects("RAM end", "crosses", [] { extract(make(Bytes(64), 0x801ffff0)); });
  CHECK(extract(make(Bytes(64), 0x801fffc0)).banks.empty());
  CHECK(extract(make(Bytes(4096))).banks.empty());
  Bytes falseMagic(0x100); magic(falseMagic, 0, "pBAV");
  auto result = extract(make(falseMagic));
  CHECK(result.candidates == 1 && result.diagnostics.size() == 1 && result.banks.empty());
  CHECK(result.diagnostics[0].code == "unloaded-or-invalid-range");
  // One never-loaded byte in the body must not become a silent valid sample.
  const auto hole = std::size_t(0xe20 + 20);
  const Bytes before(original.begin(), original.begin() + hole), after(original.begin() + hole + 1, original.end());
  result = psf::extract({{"a.psf", make(before, 0x80010000, "_lib2=after.psflib")},
    {"after.psflib", make(after, static_cast<std::uint32_t>(0x80010000 + hole + 1))}}, "a.psf");
  CHECK(result.banks.empty() && result.diagnostics.size() == 1);
  CHECK(result.diagnostics[0].code == "unloaded-or-invalid-range");
  // Metadata may parse while all assigned sample payloads are unsupported.
  auto unsupported = original; unsupported[0xe20 + 16] = 0xf0; unsupported[0xe20 + 96] = 0xf0;
  result = extract(make(unsupported));
  CHECK(result.banks.empty() && result.diagnostics[0].code == "no-supported-tone");
}
void conservativeBodyGuards()
{
  constexpr std::size_t nested = 0xe20 + 128;
  auto result = extract(fixture("conflict-vab.psf"));
  CHECK(result.banks.size() == 1 && result.banks[0].address == 0x10000 + nested);
  CHECK(result.diagnostics.size() == 1 && result.diagnostics[0].code == "ambiguous-vab-body");
  auto data = fixture("conflict-vab.bin"); w32(data, nested + 12, 0x3000);
  result = extract(make(data));
  CHECK(result.banks.empty() && result.diagnostics.size() == 2);
  CHECK(result.diagnostics[0].code == "ambiguous-vab-body");
  CHECK(result.diagnostics[1].code == "unloaded-or-invalid-range");
  data = fixture("conflict-vab.bin");
  data[nested + 0xe20 + 16] = 0xf0; data[nested + 0xe20 + 96] = 0xf0;
  result = extract(make(data));
  CHECK(result.banks.empty() && result.diagnostics[0].code == "ambiguous-vab-body");
  CHECK(result.diagnostics[1].code == "no-supported-tone");
  // A false magic / invalid header must not establish a body conflict.
  data = fixture("conflict-vab.bin"); data[nested + 4] = 99;
  result = extract(make(data));
  CHECK(result.banks.size() == 1 && result.banks[0].address == 0x10000);
  CHECK(result.diagnostics[0].code == "invalid-vab");
  result = extract(fixture("conflict-seq.psf"));
  CHECK(result.banks.empty() && result.diagnostics[0].code == "ambiguous-seq-body");
  data = fixture("conflict-seq.bin"); data[nested + 13] = data[nested + 14] = 0;
  CHECK(extract(make(data)).diagnostics[0].code == "ambiguous-seq-body");
  for (unsigned variant = 0; variant < 4; ++variant) {
    data = fixture("conflict-seq.bin");
    if (variant == 0) data[nested + 8] = data[nested + 9] = 0; // resolution zero
    if (variant == 1) data[nested + 10] = data[nested + 11] = data[nested + 12] = 0; // tempo zero
    if (variant == 2) data[nested + 14] = 8; // invalid rhythm exponent
    if (variant == 3) data[nested + 7] = 2; // unsupported SEQ version
    CHECK(extract(make(data)).banks.size() == 1);
  }
  // A structured SEQ outside a VAB does not conflict with its body.
  auto original = fixture("original.vab"), seq = fixture("conflict-seq.bin");
  original.resize(original.size() + 32); original.insert(original.end(), seq.begin() + nested, seq.begin() + nested + 19);
  result = extract(make(original)); CHECK(result.banks.size() == 1 && result.diagnostics.empty());
  Bytes many(257 * 32);
  for (std::size_t n = 0; n < 257; ++n) magic(many, n * 32, "pBAV");
  rejects("candidate limit", "too many VAB", [&] { extract(make(many)); });
  for (std::size_t n = 0; n < 257; ++n) std::copy_n(seq.begin() + nested, 15, many.begin() + n * 32);
  rejects("structured SEQ limit", "too many structured SEQ", [&] { extract(make(many)); });
  const auto bank = fixture("original.vab"); Bytes banks;
  for (unsigned n = 0; n < 33; ++n) banks.insert(banks.end(), bank.begin(), bank.end());
  rejects("bank count limit", "extracted bank limit", [&] { extract(make(banks)); });
}
void cancellation()
{
  const std::vector<psf::File> files{{"a.psf", fixture("standalone.psf")}};
  rejects("already superseded", "cancelled", [&] { psf::extract(files, "a.psf", [] { return true; }); });
  // Trigger cancellation at distinct inflate/compose/scan phases. Every run
  // throws instead of exposing any partial accepted bank to a newer request.
  for (unsigned stop : {3u, 8u, 20u, 100u, 400u, 520u}) {
    unsigned calls = 0;
    rejects("supersession callback", "cancelled", [&] {
      psf::extract(files, "a.psf", [&] { return ++calls >= stop; });
    });
    CHECK(calls == stop);
  }
  unsigned checks = 0;
  const auto result = psf::extract(files, "a.psf", [&] { ++checks; return false; });
  CHECK(checks > 520 && result.banks.size() == 2);
}
} // namespace
int main()
{
  originalFixtureParity(); pathsAndDependencies(); malformedAndBounds();
  aliasesAndCoverage(); conservativeBodyGuards(); cancellation();
  std::puts("PSF1 bounded static extraction: PASS");
}
