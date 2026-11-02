// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Static extraction port of docs/playground/playground_ps1_psf.js.
// Format: https://gist.github.com/SaxxonPike/11618bd321a45a70c01febae43ff564e
#include "PsfImport.h"
#include "VabBank.h"
#include "zlib.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace tetorica::psf {
namespace {
constexpr std::size_t kRamBytes = 2 * 1024 * 1024;
constexpr std::size_t kMaxExeBytes = 2033664;
constexpr std::size_t kMaxExpandedBytes = 32 * 1024 * 1024;
using Cancel = std::function<bool()>;

[[noreturn]] void fail(const std::string& message)
{ throw std::runtime_error("PSF: " + message); }
void check(const Cancel& cancelled)
{ if (cancelled && cancelled()) fail("cancelled"); }

std::uint16_t u16(const std::uint8_t* p)
{ return std::uint16_t(p[0]) | std::uint16_t(std::uint16_t(p[1]) << 8); }
std::uint32_t u32(const std::uint8_t* p)
{ return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
    (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
std::uint32_t be32(const std::uint8_t* p)
{ return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
    (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]); }
std::string lower(std::string value)
{
  for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}
std::string trim(const std::string& value)
{
  auto space = [](char c) { return static_cast<unsigned char>(c) >= 1 &&
      static_cast<unsigned char>(c) <= 0x20; };
  std::size_t first = 0, last = value.size();
  while (first < last && space(value[first])) ++first;
  while (last > first && space(value[last - 1])) --last;
  return value.substr(first, last - first);
}
bool signature(const std::uint8_t* bytes, std::size_t size,
               std::size_t at, const char* text)
{
  const auto length = std::strlen(text);
  return at <= size && length <= size - at &&
    std::memcmp(bytes + at, text, length) == 0;
}
std::string pathOf(const std::string& value, const std::string& base = {})
{
  if (value.empty() || value.size() > 1024 || value[0] == '/' || value[0] == '\\' ||
      std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == ':'; }))
    fail("expected a relative library/file path");
  std::vector<std::string> parts;
  auto append = [&](const std::string& path) {
    std::size_t at = 0;
    while (at <= path.size()) {
      const auto end = path.find_first_of("/\\", at);
      const auto part = path.substr(at, end == std::string::npos ? end : end - at);
      if (part == "..") {
        if (parts.empty()) fail("library path escapes the selected set");
        parts.pop_back();
      } else if (!part.empty() && part != ".") parts.push_back(part);
      if (end == std::string::npos) break;
      at = end + 1;
    }
  };
  if (!base.empty()) append(base);
  append(value);
  if (parts.empty()) fail("empty file path");
  std::string result;
  for (const auto& part : parts) { if (!result.empty()) result += '/'; result += part; }
  return result;
}
bool libraryTag(const std::string& name)
{
  if (name == "_lib") return true;
  return name.size() > 4 && name.compare(0, 4, "_lib") == 0 &&
    std::all_of(name.begin() + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
}
bool validTag(const std::string& name)
{
  auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; };
  return !name.empty() && alpha(name[0]) &&
    std::all_of(name.begin(), name.end(), [&](char c) { return alpha(c) || (c >= '0' && c <= '9'); });
}
bool libraryPath(const std::string& path)
{
  const auto name = lower(path);
  auto ends = [&](const char* suffix) { const auto n = std::strlen(suffix);
    return name.size() >= n && name.compare(name.size() - n, n, suffix) == 0; };
  return ends(".psflib") || ends(".psf1lib");
}
struct Record {
  std::string path;
  const std::uint8_t* compressed = nullptr;
  std::size_t compressedBytes = 0;
  std::map<std::string, std::string> tags;
};
struct Set {
  std::vector<Record> records;
  std::map<std::string, std::size_t> paths;
  std::map<std::string, std::optional<std::size_t>> folded;
};
Set readSet(const std::vector<File>& files, const Cancel& cancelled)
{
  check(cancelled);
  if (files.empty() || files.size() > kMaxFiles) fail("select 1-512 PSF/library files");
  std::size_t total = 0;
  for (const auto& file : files) {
    if (file.bytes.size() < 16 || file.bytes.size() > kMaxFileBytes)
      fail("each file must be 16 bytes-4 MiB");
    total += file.bytes.size();
  }
  if (total > kMaxSetBytes) fail("selected files exceed 64 MiB");
  Set set;
  set.records.reserve(files.size());
  for (const auto& file : files) {
    check(cancelled);
    Record record;
    record.path = pathOf(file.path);
    if (set.paths.count(record.path)) fail("duplicate path: " + record.path);
    const auto* bytes = file.bytes.data();
    const auto size = file.bytes.size();
    if (!signature(bytes, size, 0, "PSF")) fail("expected a PSF file of at most 4 MiB");
    if (bytes[3] != 1) fail("unsupported version " + std::to_string(bytes[3]) + " (PSF1 only; PSF2 is for PS2)");
    const std::size_t reserved = u32(bytes + 4), compressedBytes = u32(bytes + 8);
    if (reserved > size - 16 || compressedBytes > size - 16 - reserved)
      fail("truncated reserved/compressed section");
    const std::size_t start = 16 + reserved, end = start + compressedBytes;
    record.compressed = bytes + start;
    record.compressedBytes = compressedBytes;
    if (crc32(0L, bytes + start, static_cast<uInt>(compressedBytes)) != u32(bytes + 12))
      fail("compressed program CRC-32 mismatch");
    if (signature(bytes, size, end, "[TAG]")) {
      if (size - end - 5 > 50000) fail("tags exceed 50,000 bytes");
      const std::string tags(reinterpret_cast<const char*>(bytes + end + 5), size - end - 5);
      if (tags.find('\0') != std::string::npos) fail("NUL in tags");
      std::size_t at = 0;
      while (at <= tags.size()) {
        const auto next = tags.find('\n', at);
        const auto line = tags.substr(at, next == std::string::npos ? next : next - at);
        const auto equal = line.find('=');
        if (equal != std::string::npos) {
          const auto name = lower(trim(line.substr(0, equal))), value = trim(line.substr(equal + 1));
          if (validTag(name)) {
            if (libraryTag(name) && record.tags.count(name)) fail("duplicate " + name + " tag");
            auto found = record.tags.find(name);
            if (found == record.tags.end()) record.tags.emplace(name, value);
            else found->second += '\n' + value;
          }
        }
        if (next == std::string::npos) break;
        at = next + 1;
      }
    }
    const auto index = set.records.size();
    set.paths.emplace(record.path, index);
    const auto key = lower(record.path);
    if (set.folded.count(key)) set.folded[key] = std::nullopt;
    else set.folded.emplace(key, index);
    set.records.push_back(std::move(record));
  }
  return set;
}

std::vector<std::uint8_t> inflateProgram(const Record& record, const Cancel& cancelled)
{
  check(cancelled);
  if (!record.compressedBytes) return {};
  z_stream stream{};
  stream.next_in = record.compressed;
  stream.avail_in = static_cast<uInt>(record.compressedBytes);
  if (inflateInit(&stream) != Z_OK) fail("zlib decompression initialization failed");
  struct End { z_stream& stream; ~End() { inflateEnd(&stream); } } end{stream};
  std::array<std::uint8_t, 16384> chunk{};
  std::vector<std::uint8_t> bytes;
  while (true) {
    check(cancelled);
    stream.next_out = chunk.data(); stream.avail_out = static_cast<uInt>(chunk.size());
    const auto status = inflate(&stream, Z_NO_FLUSH);
    const std::size_t count = chunk.size() - stream.avail_out;
    if (count > kMaxExeBytes - bytes.size()) fail("expanded PS-X EXE exceeds 2,033,664 bytes");
    bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
    check(cancelled);
    if (status == Z_STREAM_END) {
      if (stream.avail_in) fail("zlib decompression has trailing data");
      return bytes;
    }
    if (status != Z_OK || (!count && !stream.avail_in))
      fail("zlib decompression failed");
  }
}
struct Segment { std::uint32_t address = 0; std::vector<std::uint8_t> data; };
Segment exeSegment(std::vector<std::uint8_t> exe)
{
  if (exe.empty()) return {};
  if (exe.size() < 0x800 || !signature(exe.data(), exe.size(), 0, "PS-X EXE"))
    fail("program is not a PS-X EXE");
  const auto address = u32(exe.data() + 0x18), size = u32(exe.data() + 0x1c);
  if (size > exe.size() - 0x800) fail("truncated PS-X EXE text section");
  const auto physical = address & 0x1fffffff;
  if (!(address < 0x800000 || (address >= 0x80000000 && address < 0x80800000) ||
        (address >= 0xa0000000 && address < 0xa0800000)) || physical >= 0x800000)
    fail("EXE load address is outside PS1 RAM");
  const auto offset = physical & 0x1fffff;
  if (size > kRamBytes - offset) fail("EXE text crosses the end of the 2 MiB RAM image");
  Segment segment;
  segment.address = offset;
  segment.data.assign(exe.begin() + 0x800, exe.begin() + 0x800 + size);
  return segment;
}
std::string ramAddress(std::size_t at)
{
  std::ostringstream text; text << "0x" << std::hex << (0x80000000u + at); return text.str();
}
void findBanks(const std::vector<std::uint8_t>& ram,
               const std::vector<std::uint8_t>& coverage, Result& result,
               const Cancel& cancelled)
{
  struct Candidate {
    std::size_t address = 0, size = 0, headerBytes = 0;
    std::string code, reason;
  };
  std::vector<Candidate> records;
  std::vector<std::size_t> headers, sequences;
  auto loaded = [&](std::size_t start, std::size_t end) {
    return std::all_of(coverage.begin() + start, coverage.begin() + end,
                       [](std::uint8_t byte) { return byte == 1; });
  };
  for (std::size_t at = 0; at + 15 <= ram.size(); ++at) {
    if (!(at & 4095)) check(cancelled);
    if (ram[at] != 'p') continue;
    // Sony SEQ v1: magic alone is insufficient; rhythm may be all zero.
    // https://psx-spx.consoledev.net/cdromfileformats/#seq-single-sequence
    if (signature(ram.data(), ram.size(), at, "pQES") && loaded(at, at + 15) &&
        be32(ram.data() + at + 4) == 1 && (ram[at + 8] || ram[at + 9]) &&
        (ram[at + 10] || ram[at + 11] || ram[at + 12]) &&
        ((ram[at + 13] > 0 && ram[at + 13] < 128 && ram[at + 14] <= 7) ||
         (ram[at + 13] == 0 && ram[at + 14] == 0))) {
      if (sequences.size() >= 256) fail("too many structured SEQ headers");
      sequences.push_back(at);
    }
    if (at + 0x20 > ram.size() || !signature(ram.data(), ram.size(), at, "pBAV")) continue;
    check(cancelled);
    if (++result.candidates > 256) fail("too many VAB candidates");
    Candidate record;
    record.address = at; record.size = u32(ram.data() + at + 12);
    record.headerBytes = 0xa20 + std::size_t(u16(ram.data() + at + 0x12)) * 0x200;
    auto invalidRange = [&] {
      record.code = "unloaded-or-invalid-range";
      record.reason = "header/body range is invalid or not fully loaded";
    };
    if (record.size < 0xa20 || record.size > vab::kMaxFileBytes ||
        record.size > ram.size() - at || record.headerBytes > record.size ||
        !loaded(at, at + record.headerBytes)) invalidRange();
    else {
      try {
        // Keep structurally valid headers as body conflicts even if their
        // sample data is uncovered or none of their Tones is playable.
        vab::parse({ram.data() + at, record.size});
        headers.push_back(at);
        if (!loaded(at, at + record.size)) invalidRange();
      } catch (const std::exception& error) {
        record.code = "invalid-vab"; record.reason = error.what();
      }
    }
    records.push_back(std::move(record));
  }
  std::size_t extracted = 0;
  for (auto& record : records) {
    check(cancelled);
    const auto at = record.address, begin = at + record.headerBytes, end = at + record.size;
    if (record.code.empty()) {
      const auto nested = std::find_if(headers.begin(), headers.end(), [&](std::size_t other) { return other >= begin && other < end; });
      const auto sequence = std::find_if(sequences.begin(), sequences.end(), [&](std::size_t other) { return other >= begin && other < end; });
      if (nested != headers.end()) {
        record.code = "ambiguous-vab-body";
        record.reason = "assumed sample body contains another VAB header at " + ramAddress(*nested) + "; VH/VB mapping is uncertain";
      } else if (sequence != sequences.end()) {
        record.code = "ambiguous-seq-body";
        record.reason = "assumed sample body contains a structured SEQ header at " + ramAddress(*sequence) + "; VH/VB mapping is uncertain";
      }
    }
    if (record.code.empty()) {
      const auto bank = vab::parse({ram.data() + at, record.size});
      bool playable = false;
      for (std::size_t tone = 0; tone < bank.tones.size(); ++tone) {
        check(cancelled);
        try { vab::prepareTone(bank, tone); playable = true; break; }
        catch (const std::exception&) { /* Rejected Tone is not a bank failure. */ }
      }
      if (!playable) {
        record.code = "no-supported-tone"; record.reason = "no Tone passes sample validation";
      } else {
        extracted += record.size;
        if (result.banks.size() >= 32 || extracted > 8 * 1024 * 1024) fail("extracted bank limit exceeded");
        Bank found; found.address = static_cast<std::uint32_t>(at);
        found.bytes.assign(ram.begin() + at, ram.begin() + at + record.size);
        result.banks.push_back(std::move(found));
      }
    }
    if (!record.code.empty()) result.diagnostics.push_back({static_cast<std::uint32_t>(at), record.code, record.reason});
  }
}
} // namespace

std::vector<std::string> entries(const std::vector<File>& files)
{
  const auto set = readSet(files, {});
  std::vector<std::string> result;
  for (const auto& record : set.records) if (!libraryPath(record.path)) result.push_back(record.path);
  if (result.empty()) for (const auto& record : set.records) result.push_back(record.path);
  return result;
}

Result extract(const std::vector<File>& files, const std::string& entryPath, const Cancel& cancelled)
{
  const auto set = readSet(files, cancelled);
  const auto entry = set.paths.find(entryPath);
  if (entry == set.paths.end()) fail("choose a PSF entry");
  Result result; result.entryPath = entryPath;
  std::vector<std::uint8_t> ram(kRamBytes), coverage(kRamBytes);
  std::vector<std::optional<Segment>> cache(set.records.size());
  std::vector<std::size_t> stack;
  std::size_t visits = 0;
  auto library = [&](const Record& record, const std::string& name) {
    const auto slash = record.path.rfind('/');
    const auto requested = pathOf(name, slash == std::string::npos ? std::string{} : record.path.substr(0, slash));
    const auto exact = set.paths.find(requested);
    if (exact != set.paths.end()) return exact->second;
    const auto folded = set.folded.find(lower(requested));
    if (folded != set.folded.end()) {
      if (folded->second) return *folded->second;
      fail("ambiguous library path: " + requested);
    }
    fail("missing PSFLib " + requested + " (from " + record.path + "). Select the related files together or open their folder.");
  };
  std::function<void(std::size_t)> load = [&](std::size_t index) {
    check(cancelled);
    const auto& record = set.records[index];
    if (std::find(stack.begin(), stack.end(), index) != stack.end()) fail("cyclic library reference: " + record.path);
    if (stack.size() >= 10 || ++visits > 256) fail("library depth/visit limit exceeded");
    stack.push_back(index);
    const auto primary = record.tags.find("_lib");
    if (primary != record.tags.end()) load(library(record, primary->second));
    if (!cache[index]) {
      auto exe = inflateProgram(record, cancelled);
      result.expandedBytes += exe.size();
      if (result.expandedBytes > kMaxExpandedBytes) fail("dependency programs exceed the 32 MiB expanded budget");
      cache[index] = exeSegment(std::move(exe));
    }
    const auto& segment = *cache[index];
    check(cancelled);
    std::copy(segment.data.begin(), segment.data.end(), ram.begin() + segment.address);
    std::fill_n(coverage.begin() + segment.address, segment.data.size(), 1);
    result.loadedFiles.push_back(record.path);
    for (std::size_t n = 2; ; ++n) {
      const auto tag = record.tags.find("_lib" + std::to_string(n));
      if (tag == record.tags.end()) break;
      load(library(record, tag->second));
    }
    stack.pop_back();
  };
  load(entry->second);
  check(cancelled);
  findBanks(ram, coverage, result, cancelled);
  check(cancelled);
  return result;
}
} // namespace tetorica::psf
