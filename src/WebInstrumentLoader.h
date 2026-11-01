// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "WebInstrument.h"
#include "SpuInstrument.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tetorica {
// Non-RT staging only: invalid input never issues an exchange generation and
// therefore cannot cancel a valid pending sound. The UI commits a prepared
// result only if its original project revision and exchange generation survive.
class WebInstrumentLoader final {
public:
  using Ticket=std::uint64_t;
  enum class State { Idle,Loading,Ready,Failed };
  struct Status { Ticket ticket=0; State state=State::Idle; std::string error; };
  struct Prepared {
    Ticket ticket=0;
    ImportedWebInstrument imported;
    std::unique_ptr<SpuInstrument> engine;
  };
  WebInstrumentLoader();
  ~WebInstrumentLoader();
  WebInstrumentLoader(const WebInstrumentLoader&)=delete;
  WebInstrumentLoader& operator=(const WebInstrumentLoader&)=delete;
  Ticket requestFile(std::string path);
  Ticket requestText(std::string text);
  void cancel();
  Status status() const;
  std::optional<Prepared> takeReady(Ticket ticket);
private:
  struct Job { Ticket ticket=0; std::string input; bool file=false; };
  Ticket request(std::string input,bool file);
  void run();
  mutable std::mutex mMutex;
  std::condition_variable mWake;
  bool mStopping=false;
  std::optional<Job> mJob;
  std::optional<Prepared> mPrepared;
  Status mStatus;
  std::thread mWorker;
};
} // namespace tetorica
