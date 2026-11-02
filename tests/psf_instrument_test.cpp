// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "PsfLoader.h"
#include "VabLoader.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifndef PSF_FIXTURE_DIR
#define PSF_FIXTURE_DIR "tests/fixtures/psf"
#endif
#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)

namespace {
using namespace tetorica;
using namespace std::chrono_literals;
using Bytes = std::vector<std::uint8_t>;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

Bytes read(const fs::path& path) {
  std::ifstream stream(path, std::ios::binary); CHECK(stream.good());
  return Bytes(std::istreambuf_iterator<char>(stream), {});
}
struct Files {
  fs::path root;
  Files() {
    root = fs::temp_directory_path() /
      ("tetorica-psf-instrument-" + std::to_string(Clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
  }
  ~Files() { std::error_code error; fs::remove_all(root, error); }
  std::string path(const char* relative) const { return (root / fs::u8path(relative)).u8string(); }
  void fixture(const char* source, const char* relative) const {
    const auto target = root / fs::u8path(relative); fs::create_directories(target.parent_path());
    CHECK(fs::copy_file(fs::path(PSF_FIXTURE_DIR) / source, target));
  }
  void eraseSources() const { CHECK(fs::remove_all(root) > 0 && !fs::exists(root)); }
};

PsfLoader::Status wait(PsfLoader& loader, PsfLoader::Ticket ticket, PsfLoader::State expected) {
  const auto deadline = Clock::now() + 10s;
  for (;;) {
    const auto status = loader.status(); CHECK(status.ticket == ticket);
    if (status.state != PsfLoader::State::Reading) {
      if (status.state != expected) std::fprintf(stderr, "PSF error: %s\n", status.error.c_str());
      CHECK(status.state == expected); return status;
    }
    CHECK(Clock::now() < deadline); std::this_thread::sleep_for(1ms);
  }
}
void wait(VabLoader& loader, VabLoader::Generation generation, VabLoader::State expected = VabLoader::State::Ready) {
  const auto deadline = Clock::now() + 10s;
  for (;;) {
    const auto status = loader.status(); CHECK(status.generation == generation);
    if (status.state != VabLoader::State::Loading) {
      if (status.state != expected) std::fprintf(stderr, "VAB error: %s\n", status.error.c_str());
      CHECK(status.state == expected); return;
    }
    CHECK(Clock::now() < deadline); std::this_thread::sleep_for(1ms);
  }
}
PsfLoader::Prepared take(PsfLoader& loader, PsfLoader::Ticket ticket) {
  wait(loader, ticket, PsfLoader::State::Ready);
  auto ready = loader.takeReady(ticket); CHECK(ready.has_value());
  CHECK(loader.status().state == PsfLoader::State::Idle && !loader.takeReady(ticket));
  return std::move(*ready);
}
Bytes encode(const SavedInstrumentState& state) {
  return encodeState(state.definition, state.gainPercent, state.envelope, state.reverb,
                     state.noise, state.pmon, state.morph, state.gainBoost);
}
struct Pcm { std::array<float, 1027> left{}, right{}; };
void render(SpuInstrument& engine, Pcm& pcm, int quantum) {
  for (int at = 0; at < static_cast<int>(pcm.left.size());) {
    const auto count = std::min(quantum, static_cast<int>(pcm.left.size()) - at);
    engine.render(pcm.left.data() + at, pcm.right.data() + at, count); at += count;
  }
  CHECK(engine.healthy());
}
void samePcm(SpuInstrument& actual, SpuInstrument& expected, bool fresh = false) {
  if (fresh) {
    CHECK(actual.activeVoiceCount() == 0 && expected.activeVoiceCount() == 0);
    actual.noteOn(60, 127, 3); expected.noteOn(60, 127, 3);
  }
  Pcm a, b; float peak = 0;
  for (unsigned block = 0; block < 5; ++block) {
    render(actual, a, 17); render(expected, b, 257);
    CHECK(a.left == b.left && a.right == b.right);
    for (const auto* values : {&a.left, &a.right}) for (float value : *values) {
      CHECK(std::isfinite(value)); peak = std::max(peak, std::abs(value));
    }
  }
  if (fresh) CHECK(peak > 0);
}

void extractedBanksArePortable() {
  const auto original = read(fs::path(PSF_FIXTURE_DIR) / "original.vab");
  Files files; files.fixture("standalone.psf", "standalone.psf");
  files.fixture("Demo/track.minipsf", "Demo/track.minipsf");
  files.fixture("Demo/lib/Bank.psflib", "Demo/lib/Bank.psflib");
  files.fixture("Demo/patch.psflib", "Demo/patch.psflib");
  PsfLoader psf;
  auto standalone = take(psf, psf.requestFile(files.path("standalone.psf")));
  CHECK(standalone.error.empty() && standalone.extraction.banks.size() == 2);
  CHECK(standalone.extraction.banks[0].bytes == original);
  auto mini = take(psf, psf.requestFile(files.path("Demo/track.minipsf")));
  CHECK(mini.error.empty() && mini.extraction.banks.size() == 2);
  CHECK(mini.extraction.loadedFiles == (std::vector<std::string>{
    "lib/Bank.psflib", "track.minipsf", "patch.psflib"}));
  for (std::size_t i = 0; i < mini.extraction.banks.size(); ++i)
    CHECK(mini.extraction.banks[i].bytes == standalone.extraction.banks[i].bytes);

  // No later bank selection, state restoration or note depends on disk. Even
  // MiniPSF entry reselection uses the selected set's immutable byte snapshot.
  files.eraseSources();
  mini = take(psf, psf.requestEntry(mini.document, "track.minipsf"));
  CHECK(mini.error.empty() && mini.extraction.banks[0].bytes == original);
  for (double rate : {44100., 48000.}) {
    SpuEngineExchange exchange; VabLoader loader(exchange);
    for (std::size_t bankIndex = 0; bankIndex < mini.extraction.banks.size(); ++bankIndex) {
      const auto& bytes = mini.extraction.banks[bankIndex].bytes;
      const auto bank = vab::parse({bytes.data(), bytes.size()});
      CHECK(bank.id == 23 + bankIndex);
      wait(loader, loader.requestFirstPlayable(bytes));
      auto source = exchange.snapshotDefinition();
      CHECK(source && source->bankBytes() == bytes && source->program() == 3 && source->tone() == 0);
      CHECK(exchange.beginBlock(rate)); exchange.collectRetired();
      auto direct = std::make_unique<SpuInstrument>(vab::prepareTone(bank, 0));
      CHECK(direct->prepare(rate)); samePcm(exchange.instrument(), *direct, true);

      wait(loader, loader.requestSelection(source, 3, 1)); CHECK(exchange.beginBlock(rate));
      exchange.collectRetired(); source = exchange.snapshotDefinition();
      CHECK(source->bankBytes() == bytes && source->program() == 3 && source->tone() == 1);
      direct = std::make_unique<SpuInstrument>(vab::prepareTone(bank, 1));
      CHECK(direct->prepare(rate)); samePcm(exchange.instrument(), *direct, true);

      SavedInstrumentState authored; authored.definition = source;
      authored.gainPercent = 37; authored.gainBoost = 2.25;
      authored.noise.clock = 23; authored.pmon.ratio = 2; authored.pmon.depth = .125;
      authored.morph.amount = .375; authored.reverb.preset = ReverbPreset::Room;
      authored.reverb.volumeLeft = .33; authored.reverb.volumeRight = .27;
      const auto stateBytes = encode(authored);
      const auto restoredState = decodeState({stateBytes.data(), stateBytes.size()});
      CHECK(encode(restoredState) == stateBytes);
      CHECK(restoredState.definition->kind() == InstrumentDefinition::Kind::Vab);
      CHECK(restoredState.definition->bankBytes() == bytes);
      CHECK(restoredState.definition->program() == 3 && restoredState.definition->tone() == 1);
      auto restored = prepareDefinition(*restoredState.definition);
      direct = std::make_unique<SpuInstrument>(vab::prepareTone(bank, 1));
      CHECK(restored->prepare(rate) && direct->prepare(rate)); samePcm(*restored, *direct, true);

      // Sparse Program selection uses only the embedded original bank. It
      // selects that Program's first Tone instead of retaining Tone1 from3.
      wait(loader, loader.requestProgram(restoredState.definition, 11));
      CHECK(exchange.beginBlock(rate)); exchange.collectRetired();
      source = exchange.snapshotDefinition();
      CHECK(source->program() == 11 && source->tone() == 0 && source->bankBytes() == bytes);
      wait(loader, loader.requestProgram(source, 3)); CHECK(exchange.beginBlock(rate));
      exchange.collectRetired(); source = exchange.snapshotDefinition();
      CHECK(source->program() == 3 && source->tone() == 0 && source->bankBytes() == bytes);
    }
  }
}

void firstPlayableSkipsUnsupportedTone() {
  auto bytes = read(fs::path(VAB_FIXTURE_DIR) / "spu-synth-demo.vab");
  bytes.at(0x820 + 22) = 255; bytes.at(0x820 + 23) = 0;
  const auto bank = vab::parse({bytes.data(), bytes.size()});
  bool rejected = false;
  try { (void)vab::prepareTone(bank, 0); } catch (const std::exception&) { rejected = true; }
  CHECK(rejected);
  SpuEngineExchange exchange; VabLoader loader(exchange);
  wait(loader, loader.requestFirstPlayable(bytes)); CHECK(exchange.beginBlock(48000.));
  const auto source = exchange.snapshotDefinition();
  CHECK(source->program() == 3 && source->tone() == 1 && source->bankBytes() == bytes);
  SpuInstrument expected(vab::prepareTone(bank, 1)); CHECK(expected.prepare(48000.));
  samePcm(exchange.instrument(), expected, true);
  // Exact selection still fails, preserving the accepted bank and audio.
  wait(loader, loader.request(bytes, 0), VabLoader::State::Failed);
  CHECK(!exchange.beginBlock(48000.) && exchange.snapshotDefinition() == source);
  samePcm(exchange.instrument(), expected);
}

void stagingCannotChangeActiveOrPendingSound() {
  Files files; files.fixture("standalone.psf", "standalone.psf");
  files.fixture("track.minipsf", "missing-libraries.minipsf");
  SpuEngineExchange exchange;
  const auto active = InstrumentDefinition::builtin();
  const auto activeGeneration = exchange.commitPrepared(prepareDefinition(*active), active);
  CHECK(exchange.beginBlock(48000.)); exchange.collectRetired();
  auto reference = prepareDefinition(*active); CHECK(reference->prepare(48000.));
  samePcm(exchange.instrument(), *reference, true);

  // This newer accepted import has not reached an audio block yet. PSF
  // browsing must preserve its definition AND its complete host scalar tuple.
  SavedInstrumentState accepted;
  accepted.definition = InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  accepted.gainPercent = 17; accepted.gainBoost = 3.8;
  accepted.morph.amount = .25; accepted.noise.clock = 47;
  accepted.reverb.preset = ReverbPreset::Hall; accepted.reverb.volumeRight = .7;
  const auto pendingGeneration = exchange.commitPreparedImport(prepareDefinition(*accepted.definition), accepted);
  const auto expectedState = encode(accepted);
  auto unchanged = [&] {
    CHECK(exchange.latestGeneration() == pendingGeneration && exchange.adoptedGeneration() == activeGeneration);
    const auto snapshot = exchange.snapshotAuthoring();
    CHECK(snapshot.generation == pendingGeneration && snapshot.definition == accepted.definition);
    CHECK(snapshot.importedState && encode(*snapshot.importedState) == expectedState);
    CHECK(exchange.instrument().activeVoiceCount() == 1);
    samePcm(exchange.instrument(), *reference);
  };
  PsfLoader loader;
  auto ticket = loader.requestFile(files.path("absent.psf"));
  CHECK(!wait(loader, ticket, PsfLoader::State::Failed).error.empty());
  CHECK(!loader.takeReady(ticket)); unchanged();
  auto missing = take(loader, loader.requestFile(files.path("missing-libraries.minipsf")));
  CHECK(!missing.error.empty() && missing.extraction.banks.empty()); unchanged();

  // Supersession is independent of whether the old worker has started or
  // finished. Its result cannot be consumed under either obsolete ticket.
  const auto stale = loader.requestFile(files.path("standalone.psf"));
  ticket = loader.requestEntry(missing.document, "not-in-selected-set.psf");
  CHECK(ticket > stale && !loader.takeReady(stale));
  auto failedEntry = take(loader, ticket);
  CHECK(!failedEntry.error.empty() && !loader.takeReady(stale)); unchanged();
  ticket = loader.requestFile(files.path("standalone.psf"));
  wait(loader, ticket, PsfLoader::State::Ready); loader.cancel();
  CHECK(loader.status().state == PsfLoader::State::Idle && !loader.takeReady(ticket)); unchanged();
  auto prepared = take(loader, loader.requestFile(files.path("standalone.psf")));
  CHECK(prepared.error.empty() && prepared.extraction.banks.size() == 2); unchanged();

  CHECK(exchange.beginBlock(48000.) && exchange.adoptedGeneration() == pendingGeneration);
  CHECK(exchange.adoptedImportState() && encode(*exchange.adoptedImportState()) == expectedState);
  CHECK(exchange.snapshotDefinition() == accepted.definition);
}
} // namespace

int main() {
  extractedBanksArePortable(); firstPlayableSkipsUnsupportedTone(); stagingCannotChangeActiveOrPendingSound();
  std::puts("PASS PSF worker -> bank/Tone -> native PCM and embedded state exact after sources removed; first playable; failure/supersession/cancel preserve active and accepted pending import");
}
