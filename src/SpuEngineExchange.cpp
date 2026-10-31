// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuEngineExchange.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace tetorica {

struct SpuEngineExchange::Node {
  Generation generation;
  std::unique_ptr<SpuInstrument> engine;
};

SpuEngineExchange::SpuEngineExchange()
  : mActive(new Node{0,std::make_unique<SpuInstrument>()}) {
  static_assert(std::atomic<Generation>::is_always_lock_free,
                "Audio generation counters must be lock-free");
  static_assert(std::atomic<Node*>::is_always_lock_free,
                "Audio engine pointer exchange must be lock-free");
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "Audio retirement indices must be lock-free");
}

SpuEngineExchange::~SpuEngineExchange() {
  // Destruction requires stopped producers/audio; the mutex is not a substitute
  // for that lifetime contract. All final frees occur on this non-RT caller.
  std::lock_guard<std::mutex> lock(mProducerMutex);
  delete mPending.exchange(nullptr,std::memory_order_acq_rel);
  collectRetiredLocked();
  delete mActive;
}

SpuEngineExchange::Generation SpuEngineExchange::beginRequest() {
  std::lock_guard<std::mutex> lock(mProducerMutex);
  const auto previous=mLatest.load(std::memory_order_relaxed);
  if(previous==std::numeric_limits<Generation>::max())
    throw std::overflow_error("SPU preparation generation exhausted");
  const auto next=previous+1;
  mLatest.store(next,std::memory_order_release);
  return next;
}

bool SpuEngineExchange::publish(Generation generation,std::unique_ptr<SpuInstrument> prepared) {
  std::lock_guard<std::mutex> lock(mProducerMutex);
  if(!generation || generation!=mLatest.load(std::memory_order_acquire) ||
     generation<=mAdopted.load(std::memory_order_acquire) ||
     !prepared || !prepared->healthy())return false;
  auto next=std::make_unique<Node>(Node{generation,std::move(prepared)});
  // The same producer mutex covers admission, publication and beginRequest.
  // A slow old producer cannot overwrite a newer pending generation here.
  delete mPending.exchange(next.release(),std::memory_order_acq_rel);
  return true;
}

bool SpuEngineExchange::canRetire() const noexcept {
  const auto write=mRetiredWrite.load(std::memory_order_relaxed);
  return (write+1)%kRingSize!=mRetiredRead.load(std::memory_order_acquire);
}

void SpuEngineExchange::retire(Node* node) noexcept {
  const auto write=mRetiredWrite.load(std::memory_order_relaxed);
  mRetired[write]=node;
  mRetiredWrite.store((write+1)%kRingSize,std::memory_order_release);
}

bool SpuEngineExchange::beginBlock(double hostRate) noexcept {
  if(!canRetire())return false;
  Node* candidate=mPending.exchange(nullptr,std::memory_order_acq_rel);
  if(!candidate)return false;
  // This load is the admission point. A request beginning after it may validly
  // follow this adoption; a completion already stale at this point is rejected.
  const auto admitted=mLatest.load(std::memory_order_acquire);
  if(candidate->generation!=admitted ||
     candidate->generation<=mAdopted.load(std::memory_order_relaxed) ||
     !candidate->engine->healthy() ||
     !candidate->engine->prepare(hostRate)) {
    retire(candidate);
    return false;
  }
  Node* previous=mActive;
  mActive=candidate;
  mAdopted.store(candidate->generation,std::memory_order_release);
  retire(previous);
  return true;
}

SpuInstrument& SpuEngineExchange::instrument() noexcept { return *mActive->engine; }

void SpuEngineExchange::collectRetiredLocked() {
  auto read=mRetiredRead.load(std::memory_order_relaxed);
  const auto write=mRetiredWrite.load(std::memory_order_acquire);
  while(read!=write) {
    delete mRetired[read];
    mRetired[read]=nullptr;
    read=(read+1)%kRingSize;
    mRetiredRead.store(read,std::memory_order_release);
  }
}

void SpuEngineExchange::collectRetired() {
  std::lock_guard<std::mutex> lock(mProducerMutex);
  collectRetiredLocked();
}

} // namespace tetorica
