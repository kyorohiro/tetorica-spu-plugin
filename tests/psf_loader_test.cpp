// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "PsfLoader.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

#ifndef PSF_FIXTURE_DIR
#define PSF_FIXTURE_DIR "tests/fixtures/psf"
#endif
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::abort(); } } while (0)
namespace {
using tetorica::PsfLoader;
namespace fs = std::filesystem;
namespace psf = tetorica::psf;
using Bytes = std::vector<std::uint8_t>;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::atomic<bool> gateEnabled{false};
std::atomic<std::size_t> gateSize{0};
std::mutex gateMutex;
std::condition_variable gateWake;
bool gateEntered = false, gateReleased = false;
void* allocate(std::size_t size) {
  // Deterministically stop the worker between file measurement and the read,
  // without production test hooks or sleeps that depend on thread scheduling.
  if (gateEnabled.load(std::memory_order_acquire) && size == gateSize.load()) {
    std::unique_lock<std::mutex> lock(gateMutex);
    if (gateEnabled.load(std::memory_order_relaxed)) {
      gateEntered = true; gateWake.notify_all();
      gateWake.wait(lock, [] { return gateReleased; });
    }
  }
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
struct Gate {
  explicit Gate(std::size_t size) {
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
  fs::path root;
  Files() {
    root = fs::temp_directory_path() / ("tetorica-psf-" + std::to_string(Clock::now().time_since_epoch().count()));
    CHECK(fs::create_directory(root));
  }
  ~Files() { std::error_code error; fs::remove_all(root, error); }
  std::string path(const std::string& name = {}) const { return (root / fs::u8path(name)).u8string(); }
  std::string write(const std::string& name, const Bytes& bytes) const {
    const auto file = fs::u8path(path(name)); fs::create_directories(file.parent_path());
    std::ofstream output(file, std::ios::binary); CHECK(output.good());
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    output.close(); CHECK(!output.fail()); return file.u8string();
  }
  std::string sparse(const std::string& name, std::size_t size, const Bytes& prefix = {}) const {
    const auto file = write(name, prefix); CHECK(size >= prefix.size());
    std::ofstream output(fs::u8path(file), std::ios::binary | std::ios::in | std::ios::out);
    CHECK(output.good()); output.seekp(static_cast<std::streamoff>(size - 1)); output.put(0);
    output.close(); CHECK(!output.fail()); return file;
  }
};
Bytes fixture(const char* name) {
  std::ifstream input(std::string(PSF_FIXTURE_DIR) + "/" + name, std::ios::binary); CHECK(input.good());
  return Bytes(std::istreambuf_iterator<char>(input), {});
}
PsfLoader::Status wait(PsfLoader& loader, PsfLoader::Ticket ticket, PsfLoader::State expected) {
  const auto deadline = Clock::now() + 10s;
  for (;;) {
    const auto status = loader.status(); CHECK(status.ticket == ticket);
    if (status.state != PsfLoader::State::Reading) {
      if (status.state != expected) std::fprintf(stderr, "Unexpected PSF state %d: %s\n", static_cast<int>(status.state), status.error.c_str());
      CHECK(status.state == expected); return status;
    }
    CHECK(Clock::now() < deadline); std::this_thread::sleep_for(1ms);
  }
}
PsfLoader::Prepared take(PsfLoader& loader, PsfLoader::Ticket ticket) {
  wait(loader, ticket, PsfLoader::State::Ready);
  auto result = loader.takeReady(ticket); CHECK(result.has_value());
  CHECK(!loader.takeReady(ticket)); CHECK(loader.status().state == PsfLoader::State::Idle);
  return std::move(*result);
}
void successAndImmutableSnapshot() {
  Files files; const auto standalone = fixture("standalone.psf"), original = fixture("original.vab");
  const auto direct = files.write("音色.PSF", standalone);
  files.write("Demo/track.minipsf", fixture("track.minipsf"));
  files.write("Demo/lib/Bank.psflib", fixture("Bank.psflib"));
  files.write("Demo/patch.psflib", fixture("patch.psflib"));
  files.write("ignored.vab", Bytes{1, 2, 3});
  PsfLoader loader;
  auto result = take(loader, loader.requestFolder(files.path()));
  CHECK(result.error.empty() && result.entryPath == "Demo/track.minipsf");
  CHECK(result.document->files.size() == 4 && result.document->entries.size() == 2);
  CHECK(result.extraction.banks.size() == 2 && result.extraction.banks[0].bytes == original);
  CHECK(result.extraction.loadedFiles == (std::vector<std::string>{"Demo/lib/Bank.psflib", "Demo/track.minipsf", "Demo/patch.psflib"}));
  CHECK(result.document->root == fs::canonical(files.root).u8string());
  auto document = result.document;
  result = take(loader, loader.requestFile(direct));
  CHECK(result.error.empty() && result.entryPath == "音色.PSF");
  CHECK(result.extraction.banks[0].bytes == original);
  // Later entry selection reads the captured bytes, not edited or deleted disk.
  fs::remove_all(files.root); CHECK(fs::create_directory(files.root));
  result = take(loader, loader.requestEntry(document, "Demo/track.minipsf"));
  CHECK(result.document == document && result.error.empty());
  CHECK(result.extraction.banks[0].bytes == original && result.extraction.banks.size() == 2);
  result = take(loader, loader.requestEntry(document, "not-selected.psf"));
  CHECK(result.document == document && !result.error.empty() && result.entryPath == "not-selected.psf");
}
Bytes referencing(const std::string& library) {
  Bytes result(16); result[0] = 'P'; result[1] = 'S'; result[2] = 'F'; result[3] = 1;
  const auto tags = "[TAG]_lib=" + library + "\n"; result.insert(result.end(), tags.begin(), tags.end());
  return result;
}
void missingLibraryKeepsCatalogAndExplicitRoot() {
  Files files;
  const auto song = files.write("child/song.minipsf", referencing("../Bank.psflib"));
  files.write("Bank.psflib", fixture("Bank.psflib"));
  files.write("child/valid.psf", fixture("standalone.psf"));
  PsfLoader loader;
  auto result = take(loader, loader.requestFile(song));
  CHECK(result.document && result.document->entries.size() == 2 && result.entryPath == "song.minipsf");
  CHECK(result.error.find("escapes") != std::string::npos);
  CHECK(result.error.find("Select a PSF folder") != std::string::npos && result.extraction.banks.empty());
  result = take(loader, loader.requestEntry(result.document, "valid.psf"));
  CHECK(result.error.empty() && result.extraction.banks.size() == 2);
  result = take(loader, loader.requestFolder(files.path()));
  CHECK(result.error.empty() && result.entryPath == "child/song.minipsf" && result.extraction.banks.size() == 2);
  CHECK(result.extraction.loadedFiles.front() == "Bank.psflib");
  Files missing;
  missing.write("broken.minipsf", fixture("track.minipsf"));
  missing.write("valid.psf", fixture("standalone.psf"));
  result = take(loader, loader.requestFolder(missing.path()));
  CHECK(result.error.find("missing PSFLib") != std::string::npos && result.document->entries.size() == 2);
  result = take(loader, loader.requestEntry(result.document, "valid.psf"));
  CHECK(result.error.empty() && result.extraction.banks.size() == 2);
}
void malformedAndBounds() {
  PsfLoader loader; Files files;
  CHECK(!wait(loader, loader.requestFolder(files.path()), PsfLoader::State::Failed).error.empty());
  CHECK(!wait(loader, loader.requestFile(files.path("missing.psf")), PsfLoader::State::Failed).error.empty());
  auto invalid = fixture("standalone.psf"); invalid[3] = 2;
  auto path = files.write("invalid.psf", invalid);
  CHECK(wait(loader, loader.requestFile(path), PsfLoader::State::Failed).error.find("PSF2") != std::string::npos);
  CHECK(!loader.takeReady(loader.status().ticket));
  fs::remove(fs::u8path(path));
  path = files.write("empty.psf", {});
  CHECK(wait(loader, loader.requestFile(path), PsfLoader::State::Failed).error.find("empty") != std::string::npos);
  fs::remove(fs::u8path(path));
  path = files.sparse("large.psf", psf::kMaxFileBytes + 1);
  CHECK(wait(loader, loader.requestFile(path), PsfLoader::State::Failed).error.find("4 MiB") != std::string::npos);
  fs::remove(fs::u8path(path));
  for (std::size_t i = 0; i <= psf::kMaxFiles; ++i) files.write(std::to_string(i) + ".psf", Bytes{0});
  CHECK(wait(loader, loader.requestFolder(files.path()), PsfLoader::State::Failed).error.find("512") != std::string::npos);
  Files total;
  for (unsigned i = 0; i < 17; ++i) total.sparse(std::to_string(i) + ".psf", psf::kMaxFileBytes, fixture("standalone.psf"));
  CHECK(wait(loader, loader.requestFolder(total.path()), PsfLoader::State::Failed).error.find("64 MiB") != std::string::npos);
  Files traversal;
  for (std::size_t i = 0; i <= PsfLoader::kMaxTraversalEntries; ++i) traversal.write(std::to_string(i) + ".txt", {});
  CHECK(wait(loader, loader.requestFolder(traversal.path()), PsfLoader::State::Failed).error.find("8192") != std::string::npos);
  // Malformed caller-authored snapshots must not become selectable catalogs.
  auto document = std::make_shared<PsfLoader::Document>(); document->files = {{"bad.psf", Bytes{0}}};
  CHECK(!wait(loader, loader.requestEntry(document, "bad.psf"), PsfLoader::State::Failed).error.empty());
  for (const auto action : {0, 1, 2}) {
    const auto before = loader.status().ticket; bool threw = false;
    try {
      if (action == 0) loader.requestFile(std::string("bad\0.psf", 8));
      if (action == 1) loader.requestFolder("");
      if (action == 2) loader.requestEntry({}, "entry.psf");
    } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw && loader.status().ticket == before);
  }
}
void symlinkIsolation() {
  Files selected, outside; PsfLoader loader;
  selected.write("valid.psf", fixture("standalone.psf"));
  const auto external = outside.write("outside.psf", fixture("standalone.psf"));
  std::error_code error;
  fs::create_symlink(fs::u8path(external), fs::u8path(selected.path("linked.psf")), error);
  if (error) { std::puts("SKIP symlink isolation: this account cannot create symlinks"); return; }
  fs::create_directory_symlink(outside.root, fs::u8path(selected.path("outside")), error); CHECK(!error);
  fs::create_directory_symlink(selected.root, fs::u8path(selected.path("loop")), error); CHECK(!error);
  auto result = take(loader, loader.requestFolder(selected.path()));
  CHECK(result.error.empty() && result.document->files.size() == 1 && result.entryPath == "valid.psf");
  CHECK(!wait(loader, loader.requestFile(selected.path("linked.psf")), PsfLoader::State::Failed).error.empty());
  CHECK(!wait(loader, loader.requestFolder(selected.path("outside")), PsfLoader::State::Failed).error.empty());
}
void cancellationSupersessionAndChangedReads() {
  const auto bytes = fixture("standalone.psf");
  for (bool grow : {false, true}) {
    Files files; PsfLoader loader; const auto path = files.write("changing.psf", bytes);
    Gate gate(bytes.size()); const auto ticket = loader.requestFile(path); gate.wait();
    if (grow) { std::ofstream output(fs::u8path(path), std::ios::binary | std::ios::app); output.put(0); }
    else fs::resize_file(fs::u8path(path), 16);
    gate.open(); CHECK(!wait(loader, ticket, PsfLoader::State::Failed).error.empty());
  }
  Files files; const auto path = files.write("good.psf", bytes);
  {
    PsfLoader loader; Gate gate(bytes.size());
    const auto old = loader.requestFile(path); gate.wait(); loader.cancel();
    CHECK(loader.status().state == PsfLoader::State::Idle && loader.status().ticket > old);
    gate.open(); const auto latest = loader.requestFile(path);
    const auto result = take(loader, latest); CHECK(result.error.empty() && !loader.takeReady(old));
  }
  {
    PsfLoader loader; Gate gate(bytes.size());
    const auto old = loader.requestFile(path); gate.wait();
    const auto skipped = loader.requestFile(files.path("missing.psf"));
    const auto latest = loader.requestFile(path); CHECK(old < skipped && skipped < latest);
    gate.open(); const auto result = take(loader, latest);
    CHECK(result.error.empty() && result.extraction.banks.size() == 2);
    CHECK(!loader.takeReady(old) && !loader.takeReady(skipped));
    const auto ready = loader.requestFile(path); wait(loader, ready, PsfLoader::State::Ready);
    const auto failure = loader.requestFile(files.path("missing.psf"));
    wait(loader, failure, PsfLoader::State::Failed); CHECK(!loader.takeReady(ready));
  }
  // Destructor joins an in-flight reader after cancellation; it never detaches.
  {
    auto loader = std::make_unique<PsfLoader>(); Gate gate(bytes.size());
    loader->requestFile(path); gate.wait();
    std::thread destroy([owned = std::move(loader)]() mutable { owned.reset(); });
    gate.open(); destroy.join();
  }
}
}
int main() {
  successAndImmutableSnapshot(); missingLibraryKeepsCatalogAndExplicitRoot();
  malformedAndBounds(); symlinkIsolation(); cancellationSupersessionAndChangedReads();
  std::puts("PASS PSF worker: UTF8 files/folders, immutable entry selection, exact original VAB, missing/parent library catalog retention, malformed/4MiB/64MiB/512/8192 bounds, symlink isolation, changed reads, cancellation/latest-only publication, joined destruction");
}
