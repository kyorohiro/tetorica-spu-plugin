// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "GainSmoother.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <type_traits>
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
using tetorica::AudioGainSmoother;
namespace {
bool close(double actual, double expected) { return std::abs(actual - expected) < 2e-13; }

void timingsAndExactEndpoints() {
  static_assert(noexcept(std::declval<AudioGainSmoother&>().reset(44100., 1.)));
  static_assert(noexcept(std::declval<AudioGainSmoother&>().setTarget(0.)));
  static_assert(noexcept(std::declval<AudioGainSmoother&>().next()));
  static_assert(std::is_trivially_copyable<AudioGainSmoother>::value);
  for (const auto setting : {std::array<unsigned, 2>{8000, 40}, {44100, 221},
                            {48000, 240}, {96000, 480}, {192000, 960}}) {
    const auto rate = setting[0], frames = setting[1];
    CHECK(double(frames) / rate >= .005);
    CHECK(double(frames) / rate < .005 + 1. / rate);
    for (const auto endpoints : {std::array<double, 2>{1., 0.}, {0., 1.}, {.37, .61}}) {
      AudioGainSmoother gain;
      gain.reset(rate, endpoints[0]);
      CHECK(gain.current() == endpoints[0] && gain.next() == endpoints[0]);
      gain.setTarget(endpoints[1]);
      CHECK(gain.current() == endpoints[0] && gain.target() == endpoints[1]);
      CHECK(gain.rampRemaining() == frames); // No sample elapsed on a target edit.
      double previous = endpoints[0];
      for (unsigned n = 1; n <= frames; ++n) {
        const double value = gain.next();
        const double expected = endpoints[0] + (endpoints[1] - endpoints[0]) * n / frames;
        CHECK(close(value, expected) && value >= 0. && value <= 1.);
        CHECK(endpoints[1] > endpoints[0] ? value >= previous : value <= previous);
        CHECK(gain.rampRemaining() == frames - n);
        if (n < frames) CHECK(value != endpoints[1]);
        previous = value;
      }
      CHECK(gain.current() == endpoints[1] && gain.next() == endpoints[1]);
      for (int n = 0; n < 4096; ++n) CHECK(gain.next() == endpoints[1]);
    }
  }
  AudioGainSmoother initial;
  CHECK(initial.current() == 1. && initial.target() == 1. && initial.next() == 1.);
}

void retargetSameTargetAndReset() {
  AudioGainSmoother gain;
  gain.reset(48000., .2);
  gain.setTarget(.8);
  for (int n = 1; n <= 60; ++n) {
    gain.setTarget(.8); // Reading the same host value each block cannot restart.
    CHECK(close(gain.next(), .2 + .6 * n / 240));
    CHECK(gain.rampRemaining() == 240u - static_cast<unsigned>(n));
  }
  const double turning = gain.current();
  CHECK(close(turning, .35));
  gain.setTarget(.1);
  CHECK(gain.current() == turning && gain.rampRemaining() == 240);
  for (int n = 1; n <= 240; ++n)
    CHECK(close(gain.next(), turning + (.1 - turning) * n / 240));
  CHECK(gain.current() == .1 && gain.rampRemaining() == 0);
  gain.setTarget(.9);
  gain.next();
  gain.reset(96000., .375); // A lifecycle reset synchronizes immediately.
  CHECK(gain.current() == .375 && gain.target() == .375 && gain.rampRemaining() == 0);
  CHECK(gain.next() == .375);
  gain.setTarget(0.);
  CHECK(gain.rampRemaining() == 480);
  for (int n = 0; n < 480; ++n) gain.next();
  CHECK(gain.next() == 0. && !std::signbit(gain.next()));
}

template<std::size_t Size>
std::array<double, 2301> partitioned(const std::array<unsigned, Size>& chunks) {
  AudioGainSmoother gain;
  gain.reset(44100., .5);
  std::array<double, 2301> result{};
  double target = 0.;
  std::size_t at = 0, chunk = 0;
  while (at < result.size()) {
    gain.setTarget(target);
    const auto end = std::min(result.size(), at + chunks[chunk++ % chunks.size()]);
    for (; at < end; ++at) {
      if (at == 123) target = .8;
      if (at == 511) target = .4;
      if (at == 1024) target = 1.;
      gain.setTarget(target);
      result[at] = gain.next();
    }
  }
  return result;
}
void partitionInvariance() {
  const auto whole = partitioned(std::array<unsigned, 1>{2301});
  CHECK(whole == partitioned(std::array<unsigned, 1>{1}));
  CHECK(whole == partitioned(std::array<unsigned, 5>{7, 256, 13, 1024, 1}));
  AudioGainSmoother first, independent;
  first.reset(48000., 1.); independent.reset(48000., .25);
  first.setTarget(0.);
  for (int n = 0; n < 513; ++n) { first.next(); CHECK(independent.next() == .25); }
  CHECK(first.current() == 0.);
}

void invalidValuesAndRates() {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto inf = std::numeric_limits<double>::infinity();
  for (double value : {-1., -0., nan, inf, -inf}) {
    AudioGainSmoother gain;
    gain.reset(48000., value);
    CHECK(gain.current() == 0. && gain.target() == 0. && gain.next() == 0.);
    gain.reset(48000., 1.);
    gain.setTarget(value);
    for (int n = 0; n < 240; ++n) CHECK(std::isfinite(gain.next()));
    CHECK(gain.current() == 0. && gain.next() == 0.);
  }
  AudioGainSmoother clipped;
  clipped.reset(48000., 100.); CHECK(clipped.current() == 1.);
  clipped.reset(48000., 0.); clipped.setTarget(2.);
  for (int n = 0; n < 240; ++n) { clipped.setTarget(100.); clipped.next(); }
  CHECK(clipped.current() == 1. && clipped.rampRemaining() == 0);
  for (double rate : {0., -1., nan, inf, -inf, std::numeric_limits<double>::max()}) {
    AudioGainSmoother gain;
    gain.reset(rate, 1.); gain.setTarget(0.);
    CHECK(gain.rampRemaining() == 221);
    for (int n = 0; n < 221; ++n) gain.next();
    CHECK(gain.current() == 0.);
  }
  for (double rate : {std::numeric_limits<double>::min(), std::numeric_limits<double>::denorm_min(), 1.}) {
    AudioGainSmoother gain;
    gain.reset(rate, 0.); gain.setTarget(1.);
    CHECK(gain.rampRemaining() == 1 && gain.next() == 1. && gain.rampRemaining() == 0);
  }
  // A representable extreme stays finite and bounded without iterating billions
  // of samples; the next larger unrepresentable duration falls back safely.
  AudioGainSmoother huge;
  huge.reset(4e11, 1.); huge.setTarget(0.);
  CHECK(huge.rampRemaining() == 2000000000u);
  CHECK(huge.next() <= 1. && huge.current() >= 0. && std::isfinite(huge.current()));
}

void callbackMemory() {
  allocations = releases = 0;
  volatile double sum = 0.;
  tracking = true;
  {
    AudioGainSmoother gain;
    for (double rate : {44100., 48000., 96000.}) {
      gain.reset(rate, .37);
      for (unsigned n = 0; n < 12000; ++n) {
        if (n % 97 == 0) gain.setTarget((n / 97) % 2 ? 0. : 1.);
        sum += gain.next();
      }
    }
  }
  tracking = false;
  CHECK(allocations == 0 && releases == 0 && std::isfinite(sum));
}
}

int main() {
  timingsAndExactEndpoints();
  retargetSameTargetAndReset();
  partitionInvariance();
  invalidValuesAndRates();
  callbackMemory();
  std::puts("PASS Gain smoother fiveMs=true firstSampleStep=true exactEndpoints=true retarget=true sameTargetNoRestart=true partitionsExact=true lifecycleReset=true boundedInvalid=true instanceIsolation=true cppNewDelete0=true");
}
