// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "WebInstrumentExport.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tetorica {
// Non-RT staging and file I/O only. The audio engine, pending exchange and host
// parameters are never changed. JSON stays owned here, not copied into the UI.
class WebInstrumentExporter final {
public:
  using Ticket=std::uint64_t;
  enum class State { Idle,Preparing,Ready,Writing,Saved,Failed };
  struct Status {
    Ticket ticket=0;
    State state=State::Idle;
    std::string error;
    std::vector<std::string> notes;
    std::string path; // Original UTF-8 path, populated after Save is accepted.
  };
  struct Preview {Ticket ticket=0;std::vector<std::string> notes;};

  WebInstrumentExporter();
  ~WebInstrumentExporter(); // Joins; an already accepted write finishes safely.
  WebInstrumentExporter(const WebInstrumentExporter&)=delete;
  WebInstrumentExporter& operator=(const WebInstrumentExporter&)=delete;

  // New preparation supersedes staged preparation/preview. It throws while
  // an accepted write is outstanding, including after that write was hidden
  // with cancel(). Call only on producer/UI, never from the audio callback.
  Ticket prepare(SavedInstrumentState state,std::string name);
  Status status() const;
  std::optional<Preview> preview(Ticket ticket) const;
  // Only the current Ready ticket may save. A false return means stale/not
  // Ready. Invalid UTF-8 filesystem spelling/empty/NUL paths throw before any
  // staging mutation. Actual path/open/write failures are reported as Failed.
  bool requestSave(Ticket ticket,std::string utf8Path);
  bool save(Ticket ticket,std::string utf8Path) { return requestSave(ticket,std::move(utf8Path)); }
  // Invalidates UI tickets and drops preparation/preview. An accepted write
  // is retained and may finish; no stale callback into an editor is performed.
  void cancel();

private:
  struct Job {
    enum class Kind { Prepare,Write };
    Kind kind=Kind::Prepare;
    Ticket ticket=0;
    SavedInstrumentState state;
    std::string text; // Name for Prepare; UTF-8 destination path for Write.
    std::optional<ExportedWebInstrument> exported;
  };
  Ticket nextTicketLocked();
  void run();
  mutable std::mutex mMutex;
  std::condition_variable mWake;
  bool mStopping=false,mWriting=false;
  std::optional<Job> mJob;
  std::optional<ExportedWebInstrument> mPrepared;
  Status mStatus;
  std::thread mWorker;
};
} // namespace tetorica
