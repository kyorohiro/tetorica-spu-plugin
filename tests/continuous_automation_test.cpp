// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "ContinuousAutomation.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <type_traits>

namespace {
thread_local bool tracking = false;
thread_local unsigned allocations = 0, releases = 0;
void* allocate(std::size_t size) {
  if (tracking) ++allocations;
  if (auto* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void release(void* value) noexcept {
  if (tracking && value) ++releases;
  std::free(value);
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { release(value); }
void operator delete[](void* value) noexcept { release(value); }
void operator delete(void* value, std::size_t) noexcept { release(value); }
void operator delete[](void* value, std::size_t) noexcept { release(value); }

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)
using Automation = tetorica::ContinuousAutomation;
using Values = Automation::Values;
namespace {
constexpr Values initial{{1., 1., 0., .25, .25}};
bool close(double a, double b) { return std::abs(a - b) <= 2e-14; }

void exactStepsAndSameOffsets() {
  Automation automation;
  automation.reset(initial);
  CHECK(automation.enqueue(0, 12, .5));
  CHECK(automation.enqueue(2, 3, .25));
  CHECK(automation.enqueue(4, 10, .8)); // Intentionally unsorted ingress.
  CHECK(automation.enqueue(2, 3, .75));
  CHECK(automation.enqueue(1, 0, 2.));
  automation.beginBlock({.5, 2., .75, .6, .8});
  CHECK(automation.value(0) == 1. && automation.value(2) == 0.);
  CHECK(automation.value(3) == .6); // An unscheduled slot follows the host.
  automation.advanceTo(0);
  CHECK(automation.value(1) == 2. && automation.nextBoundary(0, 20) == 3);
  automation.advanceTo(2); CHECK(automation.value(2) == 0.);
  automation.advanceTo(3); CHECK(automation.value(2) == .75);
  CHECK(automation.nextBoundary(3, 20) == 10);
  automation.advanceTo(10); CHECK(automation.value(4) == .8);
  automation.advanceTo(11); CHECK(automation.value(0) == 1.);
  automation.advanceTo(12); CHECK(automation.value(0) == .5);
  CHECK(automation.nextBoundary(12, 20) == 20);
  automation.advanceTo(2); // Backwards calls cannot replay or rewind edits.
  CHECK(automation.value(0) == .5 && automation.value(2) == .75);
  automation.endBlock(20);

  automation.reset(initial);
  CHECK(automation.enqueueRamp(0, 4, 4, 0., 1.));
  CHECK(automation.enqueue(0, 4, .25));
  automation.beginBlock({.25, 1., 0., .25, .25});
  automation.advanceTo(4);
  CHECK(automation.value(0) == .25 && automation.nextBoundary(4, 20) == 20);
  automation.endBlock(20);
  automation.reset(initial);
  CHECK(automation.enqueue(0, 4, .25));
  CHECK(automation.enqueueRamp(0, 4, 4, 0., 1.));
  automation.beginBlock(initial); automation.advanceTo(4);
  CHECK(automation.value(0) == 0. && automation.nextBoundary(4, 20) == 5);
}

void carriedBoundaryAndStableArrival() {
  Automation automation;
  automation.reset(initial);
  CHECK(automation.enqueue(0, 8, .3));
  CHECK(automation.enqueue(2, 16, .7));
  CHECK(automation.enqueue(1, 32, 3.8));
  const Values final{{.3, 3.8, .7, .25, .25}};
  automation.beginBlock(final); automation.advanceTo(7);
  CHECK(automation.value(0) == 1.); automation.endBlock(8);
  CHECK(automation.value(0) == 1.); // Offset==frames is still pending.
  automation.beginBlock(final); automation.advanceTo(0);
  CHECK(automation.value(0) == .3 && automation.value(2) == 0.);
  automation.endBlock(8);
  CHECK(automation.enqueue(2, 0, .9)); // New equal-time edit beats carried .7.
  automation.beginBlock(final); automation.advanceTo(0);
  CHECK(automation.value(2) == .9 && automation.value(1) == 1.);
  automation.endBlock(16);
  CHECK(automation.value(1) == 1.);
  automation.beginBlock(final); automation.advanceTo(0);
  CHECK(automation.value(1) == 3.8);
  automation.endBlock(0); // A zero-frame callback neither consumes nor shifts.
}

template<std::size_t Count>
std::array<Values, 240> render(const std::array<int, Count>& blocks, int chunk) {
  Automation automation;
  automation.reset(initial);
  CHECK(automation.enqueue(0, 12, .25));
  CHECK(automation.enqueue(1, 60, 3.8));
  CHECK(automation.enqueueRamp(2, 23, 100, 0., 1.));
  CHECK(automation.enqueue(2, 139, .2));
  CHECK(automation.enqueueRamp(3, -5, 25, 0., 1.));
  CHECK(automation.enqueue(4, 240, .9));
  const Values final{{.25, 3.8, .2, 1., .9}};
  std::array<Values, 240> result{};
  std::size_t written = 0;
  for (int frames : blocks) {
    automation.beginBlock(final);
    int at = 0;
    while (at < frames) {
      automation.advanceTo(at);
      const int end = std::min(automation.nextBoundary(at, frames), at + chunk);
      CHECK(end > at);
      Values values{};
      for (unsigned slot = 0; slot < Automation::kSlots; ++slot)
        values[slot] = automation.value(slot);
      while (at < end) { CHECK(written < result.size()); result[written++] = values; ++at; }
    }
    automation.endBlock(frames);
  }
  CHECK(written == result.size());
  automation.beginBlock(final); automation.advanceTo(0);
  CHECK(automation.value(4) == .9);
  return result;
}

void partitionInvariance() {
  const auto whole = render(std::array<int, 1>{{240}}, 240);
  CHECK(whole == render(std::array<int, 1>{{240}}, 7));
  CHECK(whole == render(std::array<int, 6>{{7, 1, 32, 13, 64, 123}}, 11));
  CHECK(whole[11][0] == 1. && whole[12][0] == .25);
  CHECK(whole[59][1] == 1. && whole[60][1] == 3.8);
  CHECK(whole[22][2] == 0. && whole[23][2] == 0. && whole[123][2] == 1.);
  CHECK(whole[138][2] == 1. && whole[139][2] == .2);
  CHECK(whole[0][3] == .2 && whole[20][3] == 1.);
  CHECK(whole[239][4] == .25);
}

void rampsAndFullRangeArithmetic() {
  Automation automation;
  automation.reset(initial);
  CHECK(automation.enqueueRamp(0, 3, 4, .2, 1.));
  automation.beginBlock(initial);
  automation.advanceTo(2); CHECK(automation.value(0) == 1.);
  automation.advanceTo(3); CHECK(automation.value(0) == .2);
  automation.advanceTo(4); CHECK(close(automation.value(0), .4));
  automation.endBlock(5);
  automation.beginBlock(initial); automation.advanceTo(0);
  CHECK(close(automation.value(0), .6) && automation.nextBoundary(0, 8) == 1);
  automation.advanceTo(1); CHECK(close(automation.value(0), .8));
  automation.advanceTo(2); CHECK(automation.value(0) == 1.);
  CHECK(automation.nextBoundary(2, 8) == 8);
  automation.endBlock(8);

  automation.reset(initial);
  CHECK(automation.enqueueRamp(2, -2, 4, 0., 1.));
  CHECK(automation.enqueueRamp(3, -20, 4, -1., .5));
  automation.beginBlock({1., 1., 1., .5, .25}); automation.advanceTo(0);
  CHECK(automation.value(2) == .5 && automation.value(3) == .5);
  automation.advanceTo(1); CHECK(automation.value(2) == .75);
  automation.advanceTo(2); CHECK(automation.value(2) == 1.);

  const auto maximum = std::numeric_limits<double>::max();
  automation.reset(initial);
  CHECK(automation.enqueueRamp(0, 0, 4, -maximum, maximum));
  CHECK(automation.enqueueRamp(1, std::numeric_limits<int>::min(),
        std::numeric_limits<std::uint32_t>::max(), -maximum, maximum));
  CHECK(automation.enqueueRamp(2, 0, 3, maximum, maximum));
  automation.beginBlock({maximum, maximum, maximum, .25, .25});
  automation.advanceTo(0);
  CHECK(automation.value(0) == -maximum && std::isfinite(automation.value(1)));
  automation.advanceTo(1); CHECK(std::isfinite(automation.value(0)) && automation.value(0) < 0.);
  automation.advanceTo(2); CHECK(automation.value(0) == 0.);
  automation.advanceTo(4);
  CHECK(automation.value(0) == maximum && automation.value(2) == maximum);
  automation.endBlock(std::numeric_limits<int>::max());
  CHECK(automation.value(1) == maximum);
}

void boundedFailureInvalidResetAndBaseline() {
  Automation automation;
  automation.reset(initial);
  CHECK(automation.enqueueRamp(2, 0, 100, 0., 1.));
  automation.beginBlock(initial); automation.advanceTo(0); automation.endBlock(1);
  for (std::size_t i = 0; i < Automation::kCapacity; ++i)
    CHECK(automation.enqueue(0, 100, static_cast<double>(i)));
  CHECK(!automation.enqueue(0, 100, 2.));
  const Values fallback{{.375, 3.8, .4, .7, .8}};
  automation.beginBlock(fallback); automation.advanceTo(0);
  for (unsigned slot = 0; slot < Automation::kSlots; ++slot)
    CHECK(automation.value(slot) == fallback[slot]);
  CHECK(automation.nextBoundary(0, 256) == 256); automation.endBlock(256);

  // Overflow can also be signaled without walking an oversized host queue.
  CHECK(automation.enqueue(0, 10, 0.)); automation.overflow();
  automation.beginBlock(fallback); automation.advanceTo(10);
  CHECK(automation.value(0) == fallback[0]); automation.endBlock(20);

  // Two individually bounded snapshots may overflow when merged with carry.
  for (std::size_t i = 0; i < Automation::kCapacity; ++i)
    CHECK(automation.enqueue(0, 100, .2));
  automation.beginBlock(fallback); automation.endBlock(1);
  CHECK(automation.enqueue(1, 1, 2.));
  automation.beginBlock(fallback); automation.advanceTo(99);
  CHECK(automation.value(0) == fallback[0] && automation.value(1) == fallback[1]);
  automation.endBlock(100);

  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  CHECK(!automation.enqueue(5, 0, 1.) && !automation.enqueue(0, -1, 1.));
  CHECK(!automation.enqueue(0, 0, nan) && !automation.enqueue(0, 0, inf));
  CHECK(!automation.enqueueRamp(5, 0, 1, 0., 1.));
  CHECK(!automation.enqueueRamp(0, 0, 0, 0., 1.));
  CHECK(!automation.enqueueRamp(0, 0, 1, nan, 1.));
  CHECK(!automation.enqueueRamp(0, 0, 1, 0., inf));
  CHECK(automation.enqueue(0, 3, .125));
  automation.beginBlock({nan, 3.8, .4, .7, .8}); automation.advanceTo(3);
  CHECK(automation.value(0) == .125 && automation.value(5) == 0.);
  automation.endBlock(4);

  CHECK(automation.enqueue(0, 0, .99));
  CHECK(automation.enqueueRamp(2, 0, 100, 0., 1.));
  automation.reset(initial); // Queued events from the old generation vanish.
  automation.beginBlock(initial); automation.advanceTo(0);
  CHECK(automation.value(0) == 1. && automation.value(2) == 0.);
  CHECK(automation.nextBoundary(0, 32) == 32); automation.endBlock(32);

  CHECK(automation.enqueue(0, 20, .99));
  CHECK(automation.enqueueRamp(2, 0, 100, 0., 1.));
  automation.beginBlock(initial); automation.advanceTo(0); automation.endBlock(4);
  CHECK(automation.enqueue(0, 3, .25)); // Current block's ingress survives import.
  automation.setBaseline(fallback); automation.beginBlock({.25, 3.8, .4, .7, .8});
  automation.advanceTo(0);
  CHECK(automation.value(0) == fallback[0] && automation.value(2) == fallback[2]);
  automation.advanceTo(3); CHECK(automation.value(0) == .25);
  CHECK(automation.nextBoundary(3, 32) == 32);
}

void concurrentIngressAndInstanceIsolation() {
  Automation automation, independent;
  automation.reset(initial); independent.reset(initial);
  std::atomic<bool> start{false};
  std::atomic<unsigned> done{0};
  const auto publish = [&](unsigned source) {
    while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
    for (unsigned event = 0; event < 25000; ++event) {
      if (event % 7)
        automation.enqueue((event + source) % 5, event % 64, (event % 101) / 100.);
      else
        automation.enqueueRamp((event + source) % 5, event % 64, 100, 0., 1.);
    }
    done.fetch_add(1, std::memory_order_release);
  };
  std::thread firstProducer(publish, 0), secondProducer(publish, 1);
  start.store(true, std::memory_order_release);
  unsigned blocks = 0;
  do {
    automation.beginBlock(initial);
    for (int frame = 0; frame < 64; ++frame) {
      automation.advanceTo(frame);
      for (unsigned slot = 0; slot < Automation::kSlots; ++slot)
        CHECK(std::isfinite(automation.value(slot)));
    }
    automation.endBlock(64);
    if (++blocks % 13 == 0) automation.reset(initial);
  } while (done.load(std::memory_order_acquire) != 2);
  firstProducer.join(); secondProducer.join();
  for (unsigned slot = 0; slot < Automation::kSlots; ++slot)
    CHECK(independent.value(slot) == initial[slot]);
}

void callbackMemoryAndNoexcept() {
  static_assert(noexcept(std::declval<Automation&>().reset(initial)));
  static_assert(noexcept(std::declval<Automation&>().setBaseline(initial)));
  static_assert(noexcept(std::declval<Automation&>().enqueue(0, 0, 1.)));
  static_assert(noexcept(std::declval<Automation&>().enqueueRamp(0, 0, 1, 0., 1.)));
  static_assert(noexcept(std::declval<Automation&>().overflow()));
  static_assert(noexcept(std::declval<Automation&>().beginBlock(initial)));
  static_assert(noexcept(std::declval<Automation&>().advanceTo(0)));
  static_assert(noexcept(std::declval<Automation&>().nextBoundary(0, 1)));
  static_assert(noexcept(std::declval<Automation&>().value(0)));
  static_assert(noexcept(std::declval<Automation&>().endBlock(1)));
  Automation automation;
  allocations = releases = 0;
  tracking = true;
  automation.reset(initial);
  for (std::size_t i = 0; i < Automation::kCapacity; ++i)
    automation.enqueue(i % 5, static_cast<int>(Automation::kCapacity - i), (i % 100) / 100.);
  automation.beginBlock(initial);
  for (int at = 0; at < 1024; ++at) automation.advanceTo(at);
  automation.endBlock(1024);
  automation.enqueueRamp(0, -3, 10000, 0., 3.8);
  automation.beginBlock(initial);
  for (int at = 0; at < 4096; ++at) automation.advanceTo(at);
  automation.endBlock(4096); automation.overflow(); automation.beginBlock(initial);
  automation.endBlock(64); automation.reset(initial);
  tracking = false;
  CHECK(allocations == 0 && releases == 0);
}
}

int main() {
  exactStepsAndSameOffsets(); carriedBoundaryAndStableArrival(); partitionInvariance();
  rampsAndFullRangeArithmetic(); boundedFailureInvalidResetAndBaseline();
  concurrentIngressAndInstanceIsolation(); callbackMemoryAndNoexcept();
  std::puts("PASS continuous automation fiveSlots=true sampleOffsets=true stableLastWins=true futureTargetSuppression=true boundaryCarry=true rampPartitionsExact=true negativeRamp=true finiteFullRange=true boundedOverflowFallback=true resetGeneration=true importBaseline=true concurrentIngress=true instanceIsolation=true callbackNewDelete0=true");
}
