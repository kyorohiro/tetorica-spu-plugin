// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "VabLoader.h"
#include "VabBank.h"
#include <chrono>
#include <exception>
#include <utility>

namespace tetorica {

VabLoader::VabLoader(SpuEngineExchange& exchange)
  : mExchange(exchange),mWorker([this]{run();}) {}

VabLoader::~VabLoader() {
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mStopping=true;
    mJob.reset();
  }
  mWake.notify_all();
  mWorker.join();
  mExchange.collectRetired();
}

VabLoader::Generation VabLoader::request(std::vector<std::uint8_t> complete,std::size_t flatTone) {
  Generation generation;
  {
    // Serialize generation issuance and job replacement together. Separate
    // locks would allow concurrent request calls to queue the older job last.
    std::lock_guard<std::mutex> lock(mMutex);
    generation=mExchange.beginRequest();
    mJob.reset();
    if(complete.size()>vab::kMaxFileBytes) {
      mStatus={generation,State::Failed,"VAB input exceeds the 1 MiB limit"};
    } else {
      mStatus={generation,State::Loading,{}};
      mJob.emplace(Job{generation,std::move(complete),flatTone,false});
    }
  }
  mWake.notify_one();
  return generation;
}

VabLoader::Generation VabLoader::requestBuiltin() {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    generation=mExchange.beginRequest();
    mStatus={generation,State::Loading,{}};
    mJob.emplace(Job{generation,{},0,true});
  }
  mWake.notify_one();
  return generation;
}

VabLoader::Status VabLoader::status() const {
  std::lock_guard<std::mutex> lock(mMutex);
  return mStatus;
}

bool VabLoader::current(Generation generation) const {
  std::lock_guard<std::mutex> lock(mMutex);
  return !mStopping&&mStatus.generation==generation;
}

void VabLoader::finish(Generation generation,std::unique_ptr<SpuInstrument> engine,std::string error) {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStopping || mStatus.generation!=generation)return;
  if(error.empty()) {
    if(mExchange.publish(generation,std::move(engine)))mStatus={generation,State::Ready,{}};
    else mStatus={generation,State::Failed,"SPU preparation was superseded before publication"};
  } else mStatus={generation,State::Failed,std::move(error)};
}

void VabLoader::run() {
  for(;;) {
    std::optional<Job> job;
    {
      std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait_for(lock,std::chrono::milliseconds(20),[this]{return mStopping||mJob.has_value();});
      if(mStopping)return;
      job=std::move(mJob);
      mJob.reset();
    }
    // Audio never notifies a condition variable. Periodic collection also
    // reclaims engines when no new file request arrives.
    mExchange.collectRetired();
    if(!job || !current(job->generation))continue;
    try {
      std::unique_ptr<SpuInstrument> prepared;
      if(job->builtin)prepared=std::make_unique<SpuInstrument>();
      else {
        auto bank=vab::parse({job->bytes.data(),job->bytes.size()});
        if(!current(job->generation))continue;
        auto tone=vab::prepareTone(bank,job->flatTone);
        if(!current(job->generation))continue;
        prepared=std::make_unique<SpuInstrument>(tone);
      }
      finish(job->generation,std::move(prepared),{});
    } catch(const std::exception& error) {
      finish(job->generation,nullptr,error.what());
    } catch(...) {
      finish(job->generation,nullptr,"Unknown VAB preparation failure");
    }
  }
}

} // namespace tetorica
