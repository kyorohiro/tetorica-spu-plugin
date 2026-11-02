// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "PsfImport.h"
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tetorica {

// Non-RT selected-folder snapshot and extraction worker. No engine/exchange
// reference: neither an import failure nor a stale job can change active audio.
// Destroy off the audio thread; destruction cancels and joins pending work.
class PsfLoader final {
public:
  using Ticket = std::uint64_t;
  enum class State { Idle, Reading, Ready, Failed };
  struct Document {
    std::vector<psf::File> files;
    std::vector<std::string> entries;
    std::string root;
  };
  struct Prepared {
    std::shared_ptr<const Document> document;
    psf::Result extraction;
    std::string entryPath;
    // A valid indexed set survives a missing library or failed extraction so
    // the user can select another entry. Such a result is Ready, not Failed.
    std::string error;
  };
  struct Status { Ticket ticket = 0; State state = State::Idle; std::string error; };
  static constexpr std::size_t kMaxTraversalEntries = 8192;

  PsfLoader();
  ~PsfLoader();
  PsfLoader(const PsfLoader&) = delete;
  PsfLoader& operator=(const PsfLoader&) = delete;

  // The selected file's parent is the root. Libraries above it require an
  // explicit folder selection; no library reference ever opens another file.
  Ticket requestFile(std::string entryFullPath);
  // Recursively snapshots only PSF-related regular files below this folder,
  // without following symlink files/directories, then extracts the first entry.
  Ticket requestFolder(std::string folder);
  // Reuses a previously indexed immutable snapshot; no filesystem access.
  Ticket requestEntry(std::shared_ptr<const Document> document,
                      std::string entryRelativePath);
  void cancel();
  Status status() const;
  // Transfers once. A stale ticket is inert; consuming Ready returns to Idle.
  std::optional<Prepared> takeReady(Ticket ticket);

private:
  struct Job {
    enum class Kind { File, Folder, Entry };
    Ticket ticket = 0;
    Kind kind = Kind::File;
    std::string path;
    std::shared_ptr<const Document> document;
  };
  Ticket request(Job job);
  Ticket nextTicket() const;
  bool current(Ticket ticket) const;
  void run();

  mutable std::mutex mMutex;
  std::condition_variable mWake;
  bool mStopping = false;
  std::optional<Job> mJob;
  std::optional<Prepared> mPrepared;
  Status mStatus;
  std::thread mWorker;
};

} // namespace tetorica
