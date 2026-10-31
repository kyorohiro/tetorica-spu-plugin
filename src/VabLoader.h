// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuEngineExchange.h"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace tetorica {

// Non-RT control object. One worker prepares at most one active plus one latest
// queued job. Exchange must outlive this loader; destroy/join before destroying
// exchange. Do not instantiate or request work from an audio callback.
class VabLoader final {
public:
  using Generation=SpuEngineExchange::Generation;
  enum class State { Idle,Loading,Ready,Failed };
  struct Status {
    Generation generation=0;
    State state=State::Idle;
    std::string error;
  };

  explicit VabLoader(SpuEngineExchange& exchange);
  ~VabLoader();
  VabLoader(const VabLoader&)=delete;
  VabLoader& operator=(const VabLoader&)=delete;

  // Complete VAB bytes only; split files/dialogs/state restoration are separate.
  // Inputs are owned, never read from a file or decoded on the audio thread.
  // Oversized input starts a new generation then reports Failed, keeping audio.
  Generation request(std::vector<std::uint8_t> complete,std::size_t flatTone);
  // File operations and the <=1MiB read run on the worker. The one-argument
  // form chooses the first playable Tone; the explicit index is always exact.
  Generation requestFile(std::string utf8Path);
  Generation requestFile(std::string utf8Path,std::size_t flatTone);
  // Change selection from the embedded immutable bank, without rereading disk
  // or validating/decoding the selected sample on the UI/audio thread.
  Generation requestSelection(std::shared_ptr<const InstrumentDefinition> source,
                              std::uint32_t sparseProgram,std::uint32_t toneIndex);
  Generation requestProgram(std::shared_ptr<const InstrumentDefinition> source,
                            std::uint32_t sparseProgram);
  Generation requestBuiltin();
  // Ready means prepared and published, not yet necessarily adopted by audio.
  Status status() const;

private:
  struct Job {
    enum class Kind { Bytes,Builtin,File,Selection,Program };
    Generation generation=0;
    std::vector<std::uint8_t> bytes;
    std::size_t flatTone=0;
    Kind kind=Kind::Bytes;
    std::string path;
    std::shared_ptr<const InstrumentDefinition> source;
    std::uint32_t program=0,tone=0;
    bool firstPlayable=false;
  };
  Generation requestFileImpl(std::string path,std::size_t flatTone,bool firstPlayable);
  void run();
  bool current(Generation generation) const;
  void finish(Generation generation,std::unique_ptr<SpuInstrument> engine,std::string error);

  SpuEngineExchange& mExchange;
  mutable std::mutex mMutex;
  std::condition_variable mWake;
  bool mStopping=false;
  std::optional<Job> mJob;
  Status mStatus;
  std::thread mWorker;
};

} // namespace tetorica
