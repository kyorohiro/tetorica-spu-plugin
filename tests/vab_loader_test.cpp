// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "VabLoader.h"
#include "SpuExportSource.h"
#include "WebInstrument.h"
#include "WebInstrumentExport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#ifdef _WIN32
#include <malloc.h>
#endif

#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::abort(); } } while (0)
namespace {
using namespace tetorica;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
thread_local bool tracking = false;
thread_local unsigned allocations = 0, releases = 0;
std::atomic<bool> gateEnabled{false};
std::atomic<std::size_t> gateSize{sizeof(SpuInstrument)};
std::mutex gateMutex;
std::condition_variable gateWake;
bool gateEntered = false, gateReleased = false;
void beforeAllocate(std::size_t size) {
  if (tracking) ++allocations;
  // Pause the worker after file measurement, or just before native engine
  // construction. No production scheduling hook or race-dependent sleep.
  if (gateEnabled.load(std::memory_order_acquire) && size == gateSize.load()) {
    CHECK(!tracking);
    std::unique_lock<std::mutex> lock(gateMutex);
    if (gateEnabled.load(std::memory_order_relaxed)) {
      gateEntered = true; gateWake.notify_all();
      gateWake.wait(lock, [] { return gateReleased; });
    }
  }
}
void* allocate(std::size_t size) {
  beforeAllocate(size);
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void* allocateAligned(std::size_t size, std::size_t alignment) {
  beforeAllocate(size);
#ifdef _WIN32
  if (void* value = _aligned_malloc(size ? size : 1, alignment)) return value;
#else
  void* value = nullptr;
  if (posix_memalign(&value, alignment, size ? size : 1) == 0) return value;
#endif
  throw std::bad_alloc();
}
void release(void* value, bool aligned = false) noexcept {
  if (tracking && value) ++releases;
#ifdef _WIN32
  if (aligned) { _aligned_free(value); return; }
#else
  (void)aligned;
#endif
  std::free(value);
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { release(value); }
void operator delete[](void* value) noexcept { release(value); }
void operator delete(void* value, std::size_t) noexcept { release(value); }
void operator delete[](void* value, std::size_t) noexcept { release(value); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* value, std::align_val_t) noexcept { release(value, true); }
void operator delete[](void* value, std::align_val_t) noexcept { release(value, true); }
void operator delete(void* value, std::size_t, std::align_val_t) noexcept { release(value, true); }
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept { release(value, true); }

namespace {
struct RtProbe {
  RtProbe() { CHECK(!tracking); allocations = releases = 0; tracking = true; }
  ~RtProbe() { tracking = false; CHECK(allocations == 0 && releases == 0); }
};
struct Gate {
  explicit Gate(std::size_t size = sizeof(SpuInstrument)) {
    std::lock_guard<std::mutex> lock(gateMutex); CHECK(!gateEnabled);
    gateEntered = gateReleased = false; gateSize = size; gateEnabled = true;
  }
  void wait() {
    std::unique_lock<std::mutex> lock(gateMutex);
    CHECK(gateWake.wait_for(lock, 5s, [] { return gateEntered; }));
  }
  void open() {
    std::lock_guard<std::mutex> lock(gateMutex);
    gateEnabled = false; gateReleased = true; gateWake.notify_all();
  }
  ~Gate() { open(); }
};
struct Files {
  std::filesystem::path directory;
  Files() {
    directory = std::filesystem::temp_directory_path() /
      ("tetorica-split-vab-" + std::to_string(Clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(directory));
  }
  ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
  std::string path(const std::string& name) const { return (directory / std::filesystem::u8path(name)).u8string(); }
  std::string write(const std::string& name, const Bytes& bytes) const {
    const auto result = path(name);
    std::ofstream out(std::filesystem::u8path(result), std::ios::binary | std::ios::trunc);
    CHECK(out.good()); out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close(); CHECK(!out.fail()); return result;
  }
  std::string oversized(const std::string& name, std::size_t limit) const {
    const auto result = path(name); std::ofstream out(std::filesystem::u8path(result), std::ios::binary);
    CHECK(out.good()); out.seekp(static_cast<std::streamoff>(limit)); out.put(0); out.close(); CHECK(!out.fail());
    return result;
  }
};
Bytes fixture(const char* name) {
  std::ifstream file(std::string(VAB_FIXTURE_DIR) + '/' + name, std::ios::binary);
  CHECK(file.good()); return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
Bytes joined(const Bytes& header, const Bytes& body) {
  auto result = header; result.insert(result.end(), body.begin(), body.end()); return result;
}
struct Pcm { std::array<float, 513> left{}, right{}; };
void render(SpuInstrument& engine, Pcm& pcm, int quantum = 127) {
  for (int at = 0; at < static_cast<int>(pcm.left.size());) {
    const auto count = std::min(quantum, static_cast<int>(pcm.left.size()) - at);
    engine.render(pcm.left.data() + at, pcm.right.data() + at, count); at += count;
  }
  CHECK(engine.healthy());
}
float peak(const Pcm& pcm) {
  float result = 0;
  for (const auto* channel : {&pcm.left, &pcm.right}) for (float value : *channel) {
    CHECK(std::isfinite(value)); result = std::max(result, std::abs(value));
  }
  return result;
}
void equal(SpuInstrument& engine, SpuInstrument& reference, bool fresh = false) {
  Pcm actual, expected; float maximum = 0;
  RtProbe probe;
  if (fresh) {
    CHECK(engine.activeVoiceCount() == 0);
    engine.noteOn(60, 127, 15); reference.noteOn(60, 127, 15);
  }
  for (int block = 0; block < 8; ++block) {
    render(engine, actual, 1); render(reference, expected, 257);
    CHECK(actual.left == expected.left && actual.right == expected.right);
    maximum = std::max(maximum, peak(actual));
  }
  if (fresh) CHECK(maximum > 0);
}
bool begin(SpuEngineExchange& exchange, double rate = 48000.) {
  RtProbe probe; return exchange.beginBlock(rate);
}
VabLoader::Status wait(VabLoader& loader, VabLoader::Generation generation, VabLoader::State expected) {
  const auto deadline = Clock::now() + 5s;
  for (;;) {
    auto status = loader.status(); CHECK(status.generation == generation);
    if (status.state != VabLoader::State::Loading) {
      if (status.state != expected) std::fprintf(stderr, "Unexpected loader state: %d, error: %s\n", static_cast<int>(status.state), status.error.c_str());
      CHECK(status.state == expected); return status;
    }
    CHECK(Clock::now() < deadline); std::this_thread::sleep_for(1ms);
  }
}
MorphEndpoints endpoints() {
  MorphEndpoints result;
  result.a = makePcmCycle(WaveformShape::Triangle); result.a[7] = -.1234567f;
  result.b = makePcmCycle(WaveformShape::Square); result.b[43] = .2345678f;
  return result;
}
bool sameEndpoints(const MorphEndpoints& a, const MorphEndpoints& b) {
  return std::memcmp(a.a.data(), b.a.data(), sizeof(a.a)) == 0 &&
         std::memcmp(a.b.data(), b.b.data(), sizeof(a.b)) == 0;
}

void successfulSplitAndEmbeddedState(const Bytes& header, const Bytes& body, const Bytes& complete) {
  CHECK(header.size() == 3616 && body.size() == 128 && joined(header, body) == complete);
  const auto bank = vab::parse({complete.data(), complete.size()});
  for (double rate : {44100., 48000., 96000.}) for (std::size_t flat = 0; flat < bank.tones.size(); ++flat) {
    Files files; const auto vh = files.write("音色.VH", header), vb = files.write("音色.VB", body);
    SpuEngineExchange exchange; VabLoader loader(exchange);
    auto original = InstrumentDefinition::withMorphEndpoints(InstrumentDefinition::builtin(), endpoints());
    exchange.commitPrepared(prepareDefinition(*original), original); CHECK(begin(exchange, rate)); exchange.collectRetired();
    exchange.instrument().noteOn(69, 127); Pcm old; render(exchange.instrument(), old);
    const auto generation = loader.requestSplitFiles(vh, vb, flat); wait(loader, generation, VabLoader::State::Ready);
    CHECK(exchange.instrument().activeVoiceCount() == 1); // No adoption during worker preparation.
    const auto source = exchange.snapshotDefinition();
    CHECK(source->kind() == InstrumentDefinition::Kind::Vab && source->bankBytes() == complete);
    CHECK(source->program() == bank.tones[flat].program && source->tone() == bank.tones[flat].index);
    CHECK(sameEndpoints(source->morphEndpoints(), original->morphEndpoints()));
    CHECK(std::filesystem::remove(std::filesystem::u8path(vh)) && std::filesystem::remove(std::filesystem::u8path(vb)));
    CHECK(begin(exchange, rate));
    SpuInstrument reference(vab::prepareTone(bank, flat)); CHECK(reference.prepare(rate));
    CHECK(reference.setMorphEndpoints(original->morphEndpoints()));
    equal(exchange.instrument(), reference, true); exchange.collectRetired();

    // The normal state retains the entire original bank and all source edits,
    // never either path. Its first restored PCM works after both files vanish.
    SavedInstrumentState state; state.definition = source; state.gainPercent = 37;
    state.noise.clock = 23; state.pmon.ratio = 2; state.pmon.depth = .125; state.morph.amount = .375;
    state.reverb.preset = ReverbPreset::Room; state.reverb.useToneSend = true;
    state.reverb.volumeLeft = .33; state.reverb.volumeRight = .27;
    const auto bytes = encodeState(state.definition, state.gainPercent, state.envelope, state.reverb, state.noise, state.pmon, state.morph);
    const auto saved = decodeState({bytes.data(), bytes.size()});
    CHECK(saved.definition->bankBytes() == complete && saved.definition->program() == source->program() && saved.definition->tone() == source->tone());
    CHECK(sameEndpoints(saved.definition->morphEndpoints(), source->morphEndpoints()));
    CHECK(encodeState(saved.definition, saved.gainPercent, saved.envelope, saved.reverb, saved.noise, saved.pmon, saved.morph) == bytes);
    auto restored = prepareDefinition(*saved.definition); CHECK(restored->prepare(rate));
    SpuInstrument fresh(vab::prepareTone(bank, flat)); CHECK(fresh.prepare(rate)); equal(*restored, fresh, true);

    // Portable export remains a selected Tone, rather than a bank archive.
    const auto web = writeWebInstrument(saved, "Original split Tone");
    const auto imported = readWebInstrument(web.json);
    const auto exportedSource = extractAdpcmSource(*imported.state.definition);
    const auto selected = vab::prepareTone(bank, flat);
    CHECK(exportedSource.adpcm == selected.adpcm && exportedSource.baseFrequencyHz == selected.rootFrequencyHz);
    CHECK(exportedSource.loopStartSamples == selected.loopStartSamples && exportedSource.volumeLeft == selected.volumeLeft && exportedSource.volumeRight == selected.volumeRight);
    CHECK(exportedSource.envelope.adsr1 == selected.adsr1 && exportedSource.envelope.adsr2 == selected.adsr2 && exportedSource.reverb == selected.reverb);
    const auto selectedGeneration = loader.requestSelection(saved.definition, 11, 0);
    wait(loader, selectedGeneration, VabLoader::State::Ready); CHECK(begin(exchange, rate));
    CHECK(exchange.snapshotDefinition()->bankBytes() == complete && exchange.snapshotDefinition()->program() == 11);
    CHECK(sameEndpoints(exchange.snapshotDefinition()->morphEndpoints(), source->morphEndpoints()));
  }
}

void firstPlayableAndOriginalPortions(const Bytes& header, const Bytes& body) {
  Files files; SpuEngineExchange exchange; VabLoader loader(exchange);
  auto vh = header, vb = body; vh[4] = 6; vh[0x820 + 22] = 255; vh[0x820 + 23] = 0;
  // Reserved body prefix and ignored post-END bytes remain original data in
  // the embedded complete bank, even though neither is selected sample PCM.
  vb[0] = 0xee; vb[16 + 64 + 7] = 0xab;
  const auto hp = files.write("v6.vh", vh), bp = files.write("v6.vb", vb);
  auto generation = loader.requestSplitFiles(hp, bp); wait(loader, generation, VabLoader::State::Ready);
  CHECK(begin(exchange)); const auto source = exchange.snapshotDefinition();
  CHECK(source->program() == 3 && source->tone() == 1 && source->bankBytes() == joined(vh, vb));
  SpuInstrument reference(vab::prepareTone(vab::parse({vh.data(), vh.size()}, {vb.data(), vb.size()}), 1));
  CHECK(reference.prepare(48000.)); equal(exchange.instrument(), reference, true);
  generation = loader.requestSplitFiles(hp, bp, 0);
  CHECK(!wait(loader, generation, VabLoader::State::Failed).error.empty()); CHECK(!begin(exchange));
  equal(exchange.instrument(), reference); CHECK(exchange.snapshotDefinition() == source);
  generation = loader.requestSplitFiles(hp, bp, 9999);
  wait(loader, generation, VabLoader::State::Failed); CHECK(!begin(exchange)); equal(exchange.instrument(), reference);
}

void pendingMorphEndpointsAreRetained(const Bytes& header, const Bytes& body) {
  Files files; const auto vh = files.write("pending.vh", header), vb = files.write("pending.vb", body);
  SpuEngineExchange exchange; VabLoader loader(exchange);
  const auto authored = endpoints();
  const auto pending = loader.requestMorphEndpoints(exchange.snapshotDefinition(), authored);
  wait(loader, pending, VabLoader::State::Ready); // Deliberately do not adopt it.
  const auto split = loader.requestSplitFiles(vh, vb); CHECK(split > pending);
  wait(loader, split, VabLoader::State::Ready);
  CHECK(sameEndpoints(exchange.snapshotDefinition()->morphEndpoints(), authored));
  CHECK(begin(exchange) && sameEndpoints(exchange.instrument().morphEndpoints(), authored));
  CHECK(exchange.snapshotDefinition()->bankBytes() == joined(header, body));
}

void malformedAndFailedPending(const Bytes& header, const Bytes& body, const Bytes& complete) {
  Files files; const auto vh = files.write("good.vh", header), vb = files.write("good.vb", body);
  SpuEngineExchange exchange; VabLoader loader(exchange); SpuInstrument reference;
  CHECK(reference.prepare(48000.) && exchange.instrument().prepare(48000.) && !begin(exchange));
  exchange.instrument().noteOn(69, 127, 3); reference.noteOn(69, 127, 3); equal(exchange.instrument(), reference);
  auto shortHeader = header, shortBody = body; shortHeader.pop_back(); shortBody.pop_back();
  auto badHeader = header; badHeader[12] ^= 1;
  auto noEnd = body; for (std::size_t packet = 16; packet < 96; packet += 16) noEnd[packet + 1] &= 0xfe;
  const auto empty = files.write("empty", {}), full = files.write("complete.vab", complete);
  const auto sh = files.write("short.vh", shortHeader), sb = files.write("short.vb", shortBody);
  const auto bad = files.write("bad.vh", badHeader), malformed = files.write("missing-end.vb", noEnd);
  const auto largeHeader = files.oversized("large.vh", 68128), largeBody = files.oversized("large.vb", vab::kMaxBodyBytes);
  auto nul = vh; nul.push_back('\0'); nul += "ignored";
  const std::array<std::pair<std::string, std::string>, 13> invalid{{
    {"", vb}, {vh, ""}, {files.path("missing.vh"), vb}, {vh, files.path("missing.vb")},
    {files.directory.u8string(), vb}, {vh, empty}, {nul, vb}, {sh, vb}, {vh, sb},
    {bad, vb}, {full, vb}, {largeHeader, vb}, {vh, largeBody}}};
  for (const auto& paths : invalid) {
    const auto generation = loader.requestSplitFiles(paths.first, paths.second);
    CHECK(!wait(loader, generation, VabLoader::State::Failed).error.empty());
    CHECK(!begin(exchange)); equal(exchange.instrument(), reference);
  }
  auto generation = loader.requestSplitFiles(vb, vh); wait(loader, generation, VabLoader::State::Failed);
  CHECK(!begin(exchange)); equal(exchange.instrument(), reference);
  generation = loader.requestSplitFiles(vh, malformed, 0); wait(loader, generation, VabLoader::State::Failed);
  CHECK(!begin(exchange)); equal(exchange.instrument(), reference);
  // A newer failed VAB request invalidates an older ready request, while the
  // current instrument continues. This matches the existing complete-file API.
  const auto pending = loader.requestSplitFiles(vh, vb, 1); wait(loader, pending, VabLoader::State::Ready);
  generation = loader.requestSplitFiles(vh, files.path("missing-new.vb")); CHECK(generation > pending);
  wait(loader, generation, VabLoader::State::Failed);
  CHECK(exchange.latestGeneration() == generation && !begin(exchange) && exchange.adoptedGeneration() == 0);
  equal(exchange.instrument(), reference);
}

void changedFilesAndLatestGeneration(const Bytes& header, const Bytes& body) {
  for (bool headerFile : {false, true}) for (bool grow : {false, true}) {
    Files files; const auto vh = files.write("change.vh", header), vb = files.write("change.vb", body);
    SpuEngineExchange exchange; VabLoader loader(exchange);
    const auto changed = headerFile ? vh : vb;
    Gate gate(headerFile ? header.size() : body.size());
    const auto generation = loader.requestSplitFiles(vh, vb, 0); gate.wait();
    if (grow) { std::ofstream out(std::filesystem::u8path(changed), std::ios::binary | std::ios::app); out.put(0); out.close(); CHECK(!out.fail()); }
    else std::filesystem::resize_file(std::filesystem::u8path(changed), 16);
    gate.open(); CHECK(!wait(loader, generation, VabLoader::State::Failed).error.empty());
    CHECK(!begin(exchange) && exchange.adoptedGeneration() == 0);
  }
  Files files; const auto vh = files.write("latest.vh", header), vb = files.write("latest.vb", body);
  {
    SpuEngineExchange exchange; VabLoader loader(exchange); Gate gate;
    const auto old = loader.requestSplitFiles(vh, vb, 0); gate.wait();
    const auto latest = loader.requestSplitFiles(vh, vb, 1); CHECK(latest > old);
    gate.open(); wait(loader, latest, VabLoader::State::Ready); CHECK(begin(exchange));
    CHECK(exchange.adoptedGeneration() == latest && exchange.snapshotDefinition()->tone() == 1);
  }
  {
    SpuEngineExchange exchange; VabLoader loader(exchange);
    const auto definition = InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
    auto prepared = prepareDefinition(*definition); Gate gate(header.size());
    const auto old = loader.requestSplitFiles(vh, vb, 0); gate.wait();
    const auto restored = exchange.commitPrepared(std::move(prepared), definition);
    CHECK(restored > old && begin(exchange));
    gate.open(); wait(loader, old, VabLoader::State::Failed); CHECK(!begin(exchange));
    CHECK(exchange.adoptedGeneration() == restored && exchange.snapshotDefinition() == definition);
    auto reference = prepareDefinition(*definition); CHECK(reference->prepare(48000.)); equal(exchange.instrument(), *reference, true);
  }
}
}
int main() {
  const auto header = fixture("demo.vh"), body = fixture("demo.vb"), complete = fixture("spu-synth-demo.vab");
  successfulSplitAndEmbeddedState(header, body, complete);
  firstPlayableAndOriginalPortions(header, body);
  pendingMorphEndpointsAreRetained(header, body);
  malformedAndFailedPending(header, body, complete);
  changedFilesAndLatestGeneration(header, body);
  std::puts("PASS split VH/VB worker: exact original concatenation/full-VAB PCM at44.1/48/96k; first playable/exact Tone; UTF8 paths; original padding/Morph endpoints; state9/source files removed/Web selected Tone; malformed/bounds/active and failed-pending semantics; deterministic file grow/shrink/latest/host-restore supersession; first callback ordinary+aligned new/delete0");
}
