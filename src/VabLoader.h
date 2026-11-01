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

  // Complete VAB bytes. File dialogs and state restoration are separate.
  // Inputs are owned, never read from a file or decoded on the audio thread.
  // Oversized input starts a new generation then reports Failed, keeping audio.
  Generation request(std::vector<std::uint8_t> complete,std::size_t flatTone);
  // File operations and the <=1MiB read run on the worker. The one-argument
  // form chooses the first playable Tone; the explicit index is always exact.
  Generation requestFile(std::string utf8Path);
  Generation requestFile(std::string utf8Path,std::size_t flatTone);
  // Explicit VH header and VB body, in that order. Both bounded reads, split
  // validation and byte-exact concatenation run on the worker. The embedded
  // definition owns a complete VAB; no source path is needed after preparation.
  // As with requestFile, failure keeps active audio but supersedes older work.
  // A cancelled second picker must not call this API at all.
  Generation requestSplitFiles(std::string vhUtf8Path,std::string vbUtf8Path);
  Generation requestSplitFiles(std::string vhUtf8Path,std::string vbUtf8Path,
                               std::size_t flatTone);
  // Change selection from the embedded immutable bank, without rereading disk
  // or validating/decoding the selected sample on the UI/audio thread.
  Generation requestSelection(std::shared_ptr<const InstrumentDefinition> source,
                              std::uint32_t sparseProgram,std::uint32_t toneIndex);
  Generation requestProgram(std::shared_ptr<const InstrumentDefinition> source,
                            std::uint32_t sparseProgram);
  Generation requestBuiltin();
  // A software-authored 112-sample cycle. Validate/copy before issuing a
  // request, then encode and prime the complete engine on the worker. Invalid
  // PCM throws without cancelling valid pending work or changing current PCM.
  Generation requestPcmCycle(const PcmCycle& cycle);
  // Validate and copy before generation changes. Only the endpoints change;
  // the immutable original source and current host parameters are preserved.
  Generation requestMorphEndpoints(std::shared_ptr<const InstrumentDefinition> source,
                                   const MorphEndpoints& endpoints);
  // Ready means prepared and published, not yet necessarily adopted by audio.
  Status status() const;

private:
  struct Job {
    enum class Kind { Bytes,Builtin,File,SplitFiles,Selection,Program,PcmCycle,MorphEndpoints };
    Generation generation=0;
    std::vector<std::uint8_t> bytes;
    std::size_t flatTone=0;
    Kind kind=Kind::Bytes;
    std::string path;
    std::string bodyPath;
    std::shared_ptr<const InstrumentDefinition> source;
    std::uint32_t program=0,tone=0;
    bool firstPlayable=false;
    PcmCycle cycle{};
    tetorica::MorphEndpoints endpoints{};
  };
  Generation requestFileImpl(std::string path,std::size_t flatTone,bool firstPlayable);
  Generation requestSplitFilesImpl(std::string headerPath,std::string bodyPath,
                                   std::size_t flatTone,bool firstPlayable);
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
