// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuInstrument.h"
#include "InstrumentState.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace tetorica {

// Ownership boundary between non-RT preparation and one audio thread. Construct
// and destroy off audio, after all producers/audio users have quiesced. The
// active instrument is audio-exclusive; status readers use generation getters.
class SpuEngineExchange final {
public:
  using Generation = std::uint64_t;
  static constexpr std::size_t kRetiredCapacity = 4;

  SpuEngineExchange();
  ~SpuEngineExchange();
  SpuEngineExchange(const SpuEngineExchange&) = delete;
  SpuEngineExchange& operator=(const SpuEngineExchange&) = delete;

  // Non-RT only. Internally serialized across multiple preparing producers.
  // A new request invalidates earlier completions, retaining the active sound.
  // Adopted generations are strictly increasing. Re-publication after adoption
  // is rejected; duplicate pending work before adoption may replace that slot.
  Generation beginRequest();
  bool publish(Generation generation, std::unique_ptr<SpuInstrument> prepared,
               std::shared_ptr<const InstrumentDefinition> definition=nullptr);
  // Non-RT, transactional state restore: validate/prepare first, then commit.
  // All throwing allocation happens before any generation/pending mutation.
  // Definition/engine pairing is a trusted internal prepareDefinition contract.
  Generation commitPrepared(std::unique_ptr<SpuInstrument> prepared,
                            std::shared_ptr<const InstrumentDefinition> definition);
  // Non-RT snapshot, preferring a current pending definition to the active one.
  // A null result represents an explicitly unsaveable raw-tone publication.
  std::shared_ptr<const InstrumentDefinition> snapshotDefinition() const;
  void collectRetired();

  // Single audio thread only. No allocation, destruction, mutex or notification.
  // A full retire queue defers taking pending work. A stale/unhealthy candidate
  // or one with an invalid host rate is retired; the active engine is retained.
  // Returns true only after adopting a ready engine. Clear the old instrument's
  // deferred note events then; preserve fresh host events for this block.
  // Live controller carry/reset policy belongs to the consumer wrapper.
  bool beginBlock(double hostRate) noexcept;
  SpuInstrument& instrument() noexcept;

  Generation latestGeneration() const noexcept { return mLatest.load(std::memory_order_acquire); }
  Generation adoptedGeneration() const noexcept { return mAdopted.load(std::memory_order_acquire); }

private:
  struct Node;
  static constexpr std::size_t kRingSize = kRetiredCapacity + 1;
  bool canRetire() const noexcept;
  void retire(Node* node) noexcept;
  void collectRetiredLocked();

  mutable std::mutex mProducerMutex;
  std::atomic<Generation> mLatest{0};
  std::atomic<Generation> mAdopted{0};
  std::atomic<Generation> mRejected{0};
  std::atomic<Node*> mPending{nullptr};
  Node* mActive = nullptr; // Single audio owner; immutable ownership until swap.
  std::atomic<Node*> mObservableActive{nullptr}; // Lifetime pinned by producer mutex.
  // Non-RT-only logical publication survives the pending->active pointer gap.
  Generation mAcceptedGeneration=0;
  std::shared_ptr<const InstrumentDefinition> mAcceptedDefinition;
  std::array<Node*, kRingSize> mRetired{};
  std::atomic<std::size_t> mRetiredRead{0};  // Producer/collector-owned index.
  std::atomic<std::size_t> mRetiredWrite{0}; // Audio-owned index.
};

} // namespace tetorica
