// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tetorica {

// Bounded scheduled controls, independent of host parameter objects. Only the
// render consumer calls reset/beginBlock/advanceTo/endBlock; concurrent host
// scheduling uses enqueue. No call waits for the ingress guard or allocates.
class ContinuousAutomation final {
public:
  static constexpr unsigned kSlots = 5;
  static constexpr std::size_t kCapacity = 1024;
  using Values = std::array<double, kSlots>;

  void reset(const Values& values) noexcept {
    // A producer already inside the guard may finish, but its old generation
    // is never adopted after a restore/reset. No producer storage is touched.
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    mHandledFailures = mFailures.load(std::memory_order_acquire);
    setBaseline(values);
  }

  // A new imported tuple replaces the consumer's old sound/carry state while
  // retaining host events already queued for this upcoming block.
  void setBaseline(const Values& values) noexcept {
    mCount = mNext = 0;
    mPosition = 0;
    mRamps = {};
    for (unsigned slot = 0; slot < kSlots; ++slot)
      mValues[slot] = std::isfinite(values[slot]) ? values[slot] : 0.;
  }

  bool enqueue(unsigned slot, int offset, double value) noexcept {
    if (slot >= kSlots || offset < 0 || !std::isfinite(value))
      return false;
    return push({0, offset, slot, 0, 0, value, value});
  }

  bool enqueueRamp(unsigned slot, int offset, std::uint32_t duration,
                   double start, double end) noexcept {
    if (slot >= kSlots || !duration || !std::isfinite(start) || !std::isfinite(end))
      return false;
    // A ramp that started before this block retains its original endpoints
    // and duration; elapsed arithmetic never negates INT_MIN as an int.
    const auto elapsed = offset < 0
      ? static_cast<std::uint32_t>(std::min<std::uint64_t>(duration,
          static_cast<std::uint64_t>(-static_cast<std::int64_t>(offset)))) : 0;
    return push({0, std::max(0, offset), slot, duration, elapsed, start, end});
  }

  // An adapter can reject an oversized host queue without traversing it.
  // Public parameter objects retain the latest targets used by beginBlock.
  void overflow() noexcept {
    mFailures.fetch_add(1, std::memory_order_release);
  }

  void beginBlock(const Values& hostTargets) noexcept {
    bool failed = false;
    if (mIngressGuard.test_and_set(std::memory_order_acquire)) {
      // Ingress is busy. Forget the incomplete timeline and use the public
      // latest targets. Advancing the generation invalidates that snapshot.
      fallBack(hostTargets);
      return;
    }
    const auto generation = mGeneration.load(std::memory_order_acquire);
    if (mIncomingGeneration == generation) {
      failed = mIncomingCount > kCapacity - mCount;
      if (!failed)
        for (std::size_t i = 0; i < mIncomingCount; ++i)
          mEvents[mCount++] = mIncoming[i];
    }
    mIncomingCount = 0;
    mIngressGuard.clear(std::memory_order_release);

    const auto failures = mFailures.load(std::memory_order_acquire);
    if (failed || failures != mHandledFailures) {
      fallBack(hostTargets);
      return;
    }
    // sort uses only bounded stack storage. The ingress sequence explicitly
    // preserves last-wins order for equal offsets, including carried events.
    std::sort(mEvents.begin(), mEvents.begin() + mCount,
      [](const Event& a, const Event& b) {
        return a.offset < b.offset || (a.offset == b.offset && a.order < b.order);
      });
    std::array<bool, kSlots> scheduled{};
    for (std::size_t i = mNext; i < mCount; ++i)
      scheduled[mEvents[i].slot] = true;
    for (unsigned slot = 0; slot < kSlots; ++slot)
      if (!scheduled[slot] && !mRamps[slot].active && std::isfinite(hostTargets[slot]))
        mValues[slot] = hostTargets[slot];
  }

  // Apply all changes before rendering the sample at offset. Calls are
  // monotonic within a block; backwards/negative requests change nothing.
  void advanceTo(int offset) noexcept {
    if (offset >= 0 && offset >= mPosition)
      advance(offset, true);
  }

  int nextBoundary(int offset, int frameLimit) const noexcept {
    if (offset < 0 || frameLimit <= offset)
      return frameLimit;
    int boundary = frameLimit;
    if (mNext < mCount && mEvents[mNext].offset < boundary)
      boundary = static_cast<int>(std::max<std::int64_t>(offset, mEvents[mNext].offset));
    for (const auto& ramp : mRamps)
      if (ramp.active)
        return std::min(boundary, offset + 1);
    return boundary;
  }

  double value(unsigned slot) const noexcept {
    return slot < kSlots ? mValues[slot] : 0.;
  }

  void endBlock(int frames) noexcept {
    if (frames < 0 || frames < mPosition)
      return;
    // An event exactly at the boundary belongs to the next block. Ramps do
    // advance to that instant, so their elapsed phase survives any partition.
    advance(frames, false);
    std::size_t count = 0;
    for (std::size_t i = mNext; i < mCount; ++i) {
      mEvents[count] = mEvents[i];
      mEvents[count++].offset -= frames;
    }
    mCount = count;
    mNext = 0;
    mPosition = 0;
  }

private:
  struct Event {
    std::uint64_t order;
    std::int64_t offset;
    unsigned slot;
    std::uint32_t duration, elapsed;
    double start, end;
  };
  struct Ramp {
    double start = 0., end = 0.;
    std::uint32_t duration = 0, elapsed = 0;
    bool active = false;
  };
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                "Automation ingress requires lock-free 64-bit atomics");

  bool push(Event event) noexcept {
    const auto generation = mGeneration.load(std::memory_order_acquire);
    if (mIngressGuard.test_and_set(std::memory_order_acquire)) {
      mFailures.fetch_add(1, std::memory_order_release);
      return false;
    }
    if (mGeneration.load(std::memory_order_acquire) != generation) {
      mIngressGuard.clear(std::memory_order_release);
      return false;
    }
    if (mIncomingGeneration != generation) {
      mIncomingGeneration = generation;
      mIncomingCount = 0;
    }
    if (mIncomingCount == kCapacity) {
      mFailures.fetch_add(1, std::memory_order_release);
      mIngressGuard.clear(std::memory_order_release);
      return false;
    }
    event.order = mSequence++;
    mIncoming[mIncomingCount++] = event;
    mIngressGuard.clear(std::memory_order_release);
    return true;
  }

  static double interpolate(double start, double end, std::uint32_t elapsed,
                            std::uint32_t duration) noexcept {
    if (!elapsed) return start;
    if (elapsed >= duration) return end;
    const double fraction = static_cast<double>(elapsed) / duration;
    // Avoid overflowing (end-start) for opposite finite extrema. Equal-sign
    // endpoints use a bounded difference and preserve a convex finite result.
    if ((start <= 0. && end >= 0.) || (start >= 0. && end <= 0.))
      return (1. - fraction) * start + fraction * end;
    return start + fraction * (end - start);
  }

  void advanceRamps(std::uint64_t frames) noexcept {
    for (unsigned slot = 0; slot < kSlots; ++slot) {
      auto& ramp = mRamps[slot];
      if (!ramp.active) continue;
      ramp.elapsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        ramp.duration, static_cast<std::uint64_t>(ramp.elapsed) + frames));
      mValues[slot] = interpolate(ramp.start, ramp.end, ramp.elapsed, ramp.duration);
      ramp.active = ramp.elapsed < ramp.duration;
    }
  }

  void advance(std::int64_t offset, bool includeBoundary) noexcept {
    while (mNext < mCount && (mEvents[mNext].offset < offset ||
                             (includeBoundary && mEvents[mNext].offset == offset))) {
      const auto& event = mEvents[mNext++];
      advanceRamps(static_cast<std::uint64_t>(event.offset - mPosition));
      mPosition = event.offset;
      auto& ramp = mRamps[event.slot];
      ramp = {event.start, event.end, event.duration, event.elapsed,
              event.duration > event.elapsed};
      mValues[event.slot] = event.duration
        ? interpolate(event.start, event.end, event.elapsed, event.duration) : event.end;
    }
    advanceRamps(static_cast<std::uint64_t>(offset - mPosition));
    mPosition = offset;
  }

  void fallBack(const Values& hostTargets) noexcept {
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    mHandledFailures = mFailures.load(std::memory_order_acquire);
    mCount = mNext = 0;
    mPosition = 0;
    mRamps = {};
    for (unsigned slot = 0; slot < kSlots; ++slot)
      if (std::isfinite(hostTargets[slot])) mValues[slot] = hostTargets[slot];
  }

  std::atomic_flag mIngressGuard = ATOMIC_FLAG_INIT;
  std::atomic<std::uint64_t> mGeneration{1}, mFailures{0};
  std::uint64_t mHandledFailures = 0;
  // The following four fields and incoming array are ingress-guard protected.
  std::uint64_t mIncomingGeneration = 0, mSequence = 0;
  std::size_t mIncomingCount = 0;
  std::array<Event, kCapacity> mIncoming{};
  // Consumer-owned state. begin/end delimit one host block, irrespective of
  // internal audio rendering chunks or MIDI split points.
  std::array<Event, kCapacity> mEvents{};
  std::size_t mCount = 0, mNext = 0;
  std::int64_t mPosition = 0;
  std::array<Ramp, kSlots> mRamps{};
  Values mValues{};
};

} // namespace tetorica
