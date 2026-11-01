// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace tetorica {

struct UiMidiCommand {
  bool globalPanic = false;
  int offset = 0;
  std::uint8_t status = 0, data1 = 0, data2 = 0;
};

// One UI producer and one audio consumer. All slots are usable. The consumer
// can bound a callback's work by taking pending() once and popping at most that
// many commands: later producer writes remain queued for the next callback.
// Lifecycle operations on the consumer side must run quiescent with respect to
// the audio callback. Construction/destruction require both sides quiescent.
class UiMidiQueue final {
public:
  static constexpr std::size_t kCapacity = 1024;
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "The UI MIDI queue requires lock-free native-size indices");
  static_assert(std::atomic<bool>::is_always_lock_free,
                "The UI MIDI queue requires a lock-free overflow flag");
  static_assert((kCapacity & (kCapacity - 1)) == 0,
                "The capacity must divide the unsigned index wrap interval");

  bool push(const UiMidiCommand& command) noexcept {
    const auto write = mWrite.load(std::memory_order_relaxed);
    const auto read = mRead.load(std::memory_order_acquire);
    if (write - read >= kCapacity) {
      mOverflow.store(true, std::memory_order_release);
      return false;
    }
    mCommands[write % kCapacity] = command;
    mWrite.store(write + 1, std::memory_order_release);
    return true;
  }

  bool pop(UiMidiCommand& command) noexcept {
    const auto read = mRead.load(std::memory_order_relaxed);
    if (read == mWrite.load(std::memory_order_acquire)) return false;
    command = mCommands[read % kCapacity];
    mRead.store(read + 1, std::memory_order_release);
    return true;
  }

  // Consumer-only snapshot, bounded by kCapacity even while the producer runs.
  std::size_t pending() const noexcept {
    const auto read = mRead.load(std::memory_order_relaxed);
    return mWrite.load(std::memory_order_acquire) - read;
  }

  bool takeOverflow() noexcept {
    return mOverflow.exchange(false, std::memory_order_acq_rel);
  }

  // Clear the old overflow before freeing slots, so an overflow in a new
  // producer burst after the read advance cannot be erased by this reset.
  // Discard only commands published by the write-index snapshot: a producer
  // write published afterwards remains available for the next callback.
  void discardPending() noexcept {
    mOverflow.store(false, std::memory_order_release);
    mRead.store(mWrite.load(std::memory_order_acquire), std::memory_order_release);
  }

private:
  std::array<UiMidiCommand, kCapacity> mCommands{};
  std::atomic<std::size_t> mRead{0}, mWrite{0};
  std::atomic<bool> mOverflow{false};
};

} // namespace tetorica
