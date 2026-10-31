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
  std::shared_ptr<const InstrumentDefinition> definition;
};

SpuEngineExchange::SpuEngineExchange()
  : mActive(new Node{0,std::make_unique<SpuInstrument>(),InstrumentDefinition::builtin()}) {
  static_assert(std::atomic<Generation>::is_always_lock_free,
                "Audio generation counters must be lock-free");
  static_assert(std::atomic<Node*>::is_always_lock_free,
                "Audio engine pointer exchange must be lock-free");
  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "Audio retirement indices must be lock-free");
  mObservableActive.store(mActive,std::memory_order_release);
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

bool SpuEngineExchange::publish(Generation generation,std::unique_ptr<SpuInstrument> prepared,
                                std::shared_ptr<const InstrumentDefinition> definition) {
  std::lock_guard<std::mutex> lock(mProducerMutex);
  if(!generation || generation!=mLatest.load(std::memory_order_acquire) ||
     generation<=mAdopted.load(std::memory_order_acquire) ||
     !prepared || !prepared->healthy())return false;
  auto next=std::make_unique<Node>(Node{generation,std::move(prepared),std::move(definition)});
  // The same producer mutex covers admission, publication and beginRequest.
  // A slow old producer cannot overwrite a newer pending generation here.
  mAcceptedGeneration=generation;
  mAcceptedDefinition=next->definition;
  delete mPending.exchange(next.release(),std::memory_order_acq_rel);
  return true;
}

SpuEngineExchange::Generation SpuEngineExchange::commitPrepared(
    std::unique_ptr<SpuInstrument> prepared,std::shared_ptr<const InstrumentDefinition> definition) {
  if(!prepared||!prepared->healthy()||!definition)
    throw std::invalid_argument("SPU state: missing healthy prepared engine or definition");
  auto next=std::make_unique<Node>(Node{0,std::move(prepared),std::move(definition)});
  std::lock_guard<std::mutex> lock(mProducerMutex);
  const auto previous=mLatest.load(std::memory_order_relaxed);
  if(previous==std::numeric_limits<Generation>::max())
    throw std::overflow_error("SPU preparation generation exhausted");
  collectRetiredLocked(); // Make room for the first post-restore block to adopt.
  const auto generation=previous+1;
  next->generation=generation;
  // No throwing work after this point; a failed decode/prepare/allocation above
  // cannot cancel another valid pending load or change its authoring snapshot.
  mAcceptedGeneration=generation;
  mAcceptedDefinition=next->definition;
  mLatest.store(generation,std::memory_order_release);
  delete mPending.exchange(next.release(),std::memory_order_acq_rel);
  return generation;
}

std::shared_ptr<const InstrumentDefinition> SpuEngineExchange::snapshotDefinition() const {
  std::lock_guard<std::mutex> lock(mProducerMutex);
  // Keep the accepted definition observable while audio has taken pending but
  // is still preparing/adopting it. This shared_ptr is never touched by audio.
  if(mAcceptedGeneration==mLatest.load(std::memory_order_acquire)&&
     mAcceptedGeneration>mAdopted.load(std::memory_order_acquire)&&
     mAcceptedGeneration!=mRejected.load(std::memory_order_acquire))return mAcceptedDefinition;
  // Audio only moves nodes to active/retired ownership. Destruction takes this
  // same mutex, so the observed node stays alive while its definition is copied.
  const Node* active=mObservableActive.load(std::memory_order_acquire);
  return active?active->definition:nullptr;
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
    mRejected.store(candidate->generation,std::memory_order_release);
    retire(candidate);
    return false;
  }
  Node* previous=mActive;
  mActive=candidate;
  mObservableActive.store(candidate,std::memory_order_release);
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

std::unique_ptr<SpuInstrument> prepareDefinition(const InstrumentDefinition& definition) {
  std::unique_ptr<SpuInstrument> prepared;
  if(definition.kind()==InstrumentDefinition::Kind::Builtin)prepared=std::make_unique<SpuInstrument>();
  else if(definition.kind()==InstrumentDefinition::Kind::PcmCycle)prepared=std::make_unique<SpuInstrument>(definition.pcmCycle());
  else {
    const auto& bytes=definition.bankBytes();
    const auto bank=vab::parse({bytes.data(),bytes.size()});
    for(std::size_t i=0;i<bank.tones.size();++i)
      if(bank.tones[i].program==definition.program()&&bank.tones[i].index==definition.tone()) {
        prepared=std::make_unique<SpuInstrument>(vab::prepareTone(bank,i));break;
      }
  }
  if(!prepared)throw std::invalid_argument("SPU state: selected Program/Tone is absent");
  if(!prepared->setMorphEndpoints(definition.morphEndpoints()))
    throw std::runtime_error("SPU state: could not prepare Morph endpoints");
  return prepared;
}

} // namespace tetorica
