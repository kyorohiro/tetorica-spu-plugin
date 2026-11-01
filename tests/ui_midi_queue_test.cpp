// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "UiMidiQueue.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool tracking = false;
thread_local unsigned allocations = 0, releases = 0;
void* allocate(std::size_t size) {
  if (tracking) ++allocations;
  if (auto* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void release(void* value) noexcept {
  if (tracking && value) ++releases;
  std::free(value);
}
void* allocateAligned(std::size_t size, std::size_t alignment) {
  if (tracking) ++allocations;
#ifdef _WIN32
  if (auto* value = _aligned_malloc(size ? size : 1, alignment)) return value;
#else
  void* value = nullptr;
  if (posix_memalign(&value, alignment, size ? size : 1) == 0) return value;
#endif
  throw std::bad_alloc();
}
void releaseAligned(void* value) noexcept {
  if (tracking && value) ++releases;
#ifdef _WIN32
  _aligned_free(value);
#else
  std::free(value);
#endif
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
void operator delete(void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete(void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)
using tetorica::UiMidiCommand;
using tetorica::UiMidiQueue;
namespace {
bool same(const UiMidiCommand& a, const UiMidiCommand& b) {
  return a.globalPanic == b.globalPanic && a.offset == b.offset && a.status == b.status &&
         a.data1 == b.data1 && a.data2 == b.data2;
}
UiMidiCommand numbered(unsigned index) {
  return {index % 17 == 0, static_cast<int>(index),
    static_cast<std::uint8_t>(0x90 + index % 16),
    static_cast<std::uint8_t>(index % 128),
    static_cast<std::uint8_t>((index / 128) % 128)};
}

void exactCommandFifo() {
  static_assert(noexcept(std::declval<UiMidiQueue&>().push(UiMidiCommand{})));
  static_assert(noexcept(std::declval<UiMidiQueue&>().pop(std::declval<UiMidiCommand&>())));
  static_assert(noexcept(std::declval<const UiMidiQueue&>().pending()));
  static_assert(noexcept(std::declval<UiMidiQueue&>().takeOverflow()));
  static_assert(noexcept(std::declval<UiMidiQueue&>().discardPending()));
  static_assert(std::is_trivially_copyable<UiMidiCommand>::value);
  static_assert(UiMidiQueue::kCapacity == 1024);
  UiMidiQueue queue;
  UiMidiCommand out{false, 999, 0xff, 0xfe, 0xfd};
  const auto sentinel = out;
  CHECK(queue.pending() == 0 && !queue.pop(out) && same(out, sentinel));
  CHECK(!queue.takeOverflow());
  const UiMidiCommand note{false, 193, 0x92, 60, 127};
  const UiMidiCommand panic{true, 0, 0, 0, 0};
  const UiMidiCommand fresh{false, 0, 0x9f, 72, 91};
  for (const auto& command : {note, panic, fresh}) CHECK(queue.push(command));
  CHECK(queue.pending() == 3);
  for (const auto& command : {note, panic, fresh}) {
    CHECK(queue.pop(out) && same(out, command));
  }
  CHECK(queue.pending() == 0 && !queue.pop(out) && !queue.takeOverflow());
  // The queue transports plain command data without retiming or MIDI filtering.
  const UiMidiCommand extremes{true, std::numeric_limits<int>::min(), 0xff, 0xff, 0xff};
  CHECK(queue.push(extremes) && queue.pop(out) && same(out, extremes));
}

void capacityOverflowResetAndWrap() {
  UiMidiQueue queue;
  UiMidiCommand out;
  for (unsigned i = 0; i < UiMidiQueue::kCapacity; ++i) CHECK(queue.push(numbered(i)));
  CHECK(queue.pending() == UiMidiQueue::kCapacity);
  CHECK(!queue.push({true, 0, 0, 0, 0}) && !queue.push(numbered(9999)));
  CHECK(queue.pending() == UiMidiQueue::kCapacity && queue.takeOverflow() && !queue.takeOverflow());
  // A rejected panic cannot replace an older NoteOn or become a hidden event.
  for (unsigned i = 0; i < UiMidiQueue::kCapacity / 2; ++i)
    CHECK(queue.pop(out) && same(out, numbered(i)));
  for (unsigned i = 0; i < UiMidiQueue::kCapacity / 2; ++i)
    CHECK(queue.push(numbered(static_cast<unsigned>(UiMidiQueue::kCapacity) + i)));
  CHECK(queue.pending() == UiMidiQueue::kCapacity);
  for (unsigned i = static_cast<unsigned>(UiMidiQueue::kCapacity / 2);
       i < UiMidiQueue::kCapacity + UiMidiQueue::kCapacity / 2; ++i)
    CHECK(queue.pop(out) && same(out, numbered(i)));
  CHECK(queue.pending() == 0 && !queue.pop(out));

  // Repeated physical-array wraps with partial drains preserve the sequence.
  unsigned written = 2000, read = written;
  for (unsigned round = 0; round < 4096; ++round) {
    for (unsigned i = 0; i < 73; ++i) CHECK(queue.push(numbered(written++)));
    CHECK(queue.pending() == 73);
    for (unsigned i = 0; i < 73; ++i) CHECK(queue.pop(out) && same(out, numbered(read++)));
  }
  CHECK(read == written && queue.pending() == 0 && !queue.takeOverflow());
  for (unsigned i = 0; i < UiMidiQueue::kCapacity; ++i) CHECK(queue.push(numbered(i)));
  CHECK(!queue.push(numbered(5000)));
  queue.discardPending();
  CHECK(queue.pending() == 0 && !queue.pop(out) && !queue.takeOverflow());
  CHECK(queue.push(numbered(5001)) && queue.pop(out) && same(out, numbered(5001)));
  queue.discardPending(); queue.discardPending(); CHECK(queue.pending() == 0);
}

void producerAfterConsumerSnapshot() {
  UiMidiQueue queue;
  const UiMidiCommand previousNote{false, 0, 0x90, 60, 100};
  const UiMidiCommand stop{true, 0, 0, 0, 0};
  const UiMidiCommand nextNote{false, 0, 0x90, 64, 100};
  CHECK(queue.push(previousNote) && queue.push(stop));
  const auto blockCount = queue.pending(); CHECK(blockCount == 2);
  // Publish on the producer thread after the audio thread took its drain bound,
  // before it processes even the first command. No sleep or scheduler guess.
  std::thread producer([&] { CHECK(queue.push(nextNote)); });
  producer.join(); CHECK(queue.pending() == 3);
  unsigned held = 0;
  UiMidiCommand out;
  for (std::size_t i = 0; i < blockCount; ++i) {
    CHECK(queue.pop(out));
    if (out.globalPanic) held = 0; else ++held;
  }
  CHECK(held == 0 && queue.pending() == 1);
  CHECK(queue.pop(out) && same(out, nextNote));
  if (out.globalPanic) held = 0; else ++held;
  CHECK(held == 1 && queue.pending() == 0);
  // A quiescent lifecycle reset discards old commands but not future input.
  CHECK(queue.push(previousNote) && queue.push(stop)); queue.discardPending();
  std::thread followingProducer([&] { CHECK(queue.push(nextNote)); });
  followingProducer.join();
  CHECK(queue.pending() == 1 && queue.pop(out) && same(out, nextNote));
}

void twoThreadOrderedHandoff() {
  constexpr unsigned count = 100000;
  UiMidiQueue queue;
  std::atomic<bool> begin{false};
  unsigned producerAllocations = 0, producerReleases = 0;
  unsigned consumerAllocations = 0, consumerReleases = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  auto wait = [&] {
    CHECK(std::chrono::steady_clock::now() < deadline);
    std::this_thread::yield();
  };
  std::thread producer([&] {
    while (!begin.load(std::memory_order_acquire)) wait();
    allocations = releases = 0; tracking = true;
    for (unsigned index = 0; index < count; ++index) {
      const auto command = numbered(index);
      while (!queue.push(command)) wait(); // Retry preserves the original order.
    }
    tracking = false; producerAllocations = allocations; producerReleases = releases;
  });
  std::thread consumer([&] {
    while (!begin.load(std::memory_order_acquire)) wait();
    allocations = releases = 0; tracking = true;
    unsigned index = 0;
    UiMidiCommand out;
    while (index < count) {
      const auto blockCount = queue.pending(); CHECK(blockCount <= UiMidiQueue::kCapacity);
      if (!blockCount) { wait(); continue; }
      for (std::size_t i = 0; i < blockCount; ++i) {
        CHECK(queue.pop(out) && same(out, numbered(index)));
        ++index;
      }
    }
    tracking = false; consumerAllocations = allocations; consumerReleases = releases;
  });
  begin.store(true, std::memory_order_release);
  producer.join(); consumer.join();
  CHECK(queue.pending() == 0);
  CHECK(producerAllocations == 0 && producerReleases == 0);
  CHECK(consumerAllocations == 0 && consumerReleases == 0);
}

void callbackMemory() {
  UiMidiQueue queue;
  UiMidiCommand out;
  allocations = releases = 0; tracking = true;
  for (unsigned round = 0; round < 32; ++round) {
    for (unsigned i = 0; i < UiMidiQueue::kCapacity; ++i) CHECK(queue.push(numbered(i)));
    CHECK(!queue.push({true, 0, 0, 0, 0}) && queue.takeOverflow() && !queue.takeOverflow());
    const auto snapshot = queue.pending();
    for (std::size_t i = 0; i < snapshot; ++i) CHECK(queue.pop(out));
    CHECK(!queue.pop(out));
    CHECK(queue.push(numbered(round))); queue.discardPending();
    CHECK(queue.pending() == 0 && !queue.takeOverflow());
  }
  tracking = false;
  CHECK(allocations == 0 && releases == 0);
}
}
int main() {
  exactCommandFifo(); capacityOverflowResetAndWrap();
  producerAfterConsumerSnapshot(); twoThreadOrderedHandoff(); callbackMemory();
  std::puts("PASS UI MIDI SPSC: NoteOn/panic/fresh FIFO; deterministic snapshot late-producer handoff; full1024/overflow/reset/array wraps; two-thread100k exact sequence; callback ordinary+aligned new/delete0");
}
