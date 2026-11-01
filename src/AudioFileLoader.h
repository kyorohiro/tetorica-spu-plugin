// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "InstrumentState.h"
#include "SpuInstrument.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tetorica {
// Non-RT staging worker. It owns no exchange, and cannot change the current
// sound or invalidate pending project state. UI commits a result only after
// checking its own project revision/exchange generation. Destroy off audio;
// destruction cancels publication and joins the one in-flight preparation.
class AudioFileLoader final {
public:
  using Ticket=std::uint64_t;
  enum class State { Idle,Loading,Ready,Failed };
  struct Status {Ticket ticket=0;State state=State::Idle;std::string error;};
  struct Prepared {
    Ticket ticket=0;
    std::shared_ptr<const InstrumentDefinition> definition;
    std::unique_ptr<SpuInstrument> engine;
    bool fileImport=false;
    std::string details;
  };
  AudioFileLoader();
  ~AudioFileLoader();
  AudioFileLoader(const AudioFileLoader&)=delete;
  AudioFileLoader& operator=(const AudioFileLoader&)=delete;
  // WAV read/decode/resample and Native preparation all run on the worker.
  // Short files keep ADPCM; physical-RAM overflow uses bounded Software PCM.
  // Imported files start at C4, one-shot; A/B are retained.
  // Empty/NUL paths or invalid endpoints throw without replacing staged work.
  Ticket requestFile(std::string path,MorphEndpoints endpoints={});
  // Metadata edits stage an immutable definition; Native preparation is never
  // done on the caller. Null definitions throw without replacing staged work.
  Ticket requestDefinition(std::shared_ptr<const InstrumentDefinition> definition);
  void cancel();
  Status status() const;
  // Transfers sole prepared-engine ownership once. A stale ticket is inert.
  std::optional<Prepared> takeReady(Ticket ticket);
private:
  struct Job {
    Ticket ticket=0;
    std::string path;
    MorphEndpoints endpoints{};
    std::shared_ptr<const InstrumentDefinition> definition;
    bool file=false;
  };
  Ticket request(Job job);
  Ticket nextTicket() const;
  bool current(Ticket ticket) const;
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
