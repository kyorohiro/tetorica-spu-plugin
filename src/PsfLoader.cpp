// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "PsfLoader.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace tetorica {
namespace {
namespace fs = std::filesystem;
using Cancelled = std::function<bool()>;

void checkCancelled(const Cancelled& cancelled) {
  if (cancelled()) throw std::runtime_error("PSF loading cancelled");
}
bool related(const fs::path& path) {
  auto extension = path.extension().u8string();
  for (auto& c : extension) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return extension == ".psf" || extension == ".psf1" ||
         extension == ".minipsf" || extension == ".minipsf1" ||
         extension == ".psflib" || extension == ".psf1lib";
}
void validInput(const std::string& path, const char* message) {
  if (path.empty() || path.find('\0') != std::string::npos) throw std::invalid_argument(message);
}
bool contained(const fs::path& root, const fs::path& path) {
  auto a = root.begin(), b = path.begin();
  for (; a != root.end(); ++a, ++b) if (b == path.end() || *a != *b) return false;
  return true;
}
// The root itself cannot be a symlink. Canonicalize its ancestors once, then
// refuse all symlinks below it instead of resolving paths supplied by PSF tags.
fs::path selectedRoot(const fs::path& selected) {
  std::error_code error;
  const auto status = fs::symlink_status(selected, error);
  if (error || fs::is_symlink(status) || !fs::is_directory(status))
    throw std::runtime_error("Choose an existing PSF folder, not a symlink");
  auto root = fs::canonical(selected, error);
  if (error) throw std::runtime_error("Could not resolve the selected PSF folder");
  return root;
}
std::vector<std::uint8_t> readFile(const fs::path& root, const fs::path& path,
                                    std::size_t available, const Cancelled& cancelled) {
  checkCancelled(cancelled);
  std::error_code error;
  const auto before = fs::symlink_status(path, error);
  if (error || fs::is_symlink(before) || !fs::is_regular_file(before))
    throw std::runtime_error("PSF file is missing, is a symlink, or is not a regular file");
  const auto canonical = fs::canonical(path, error);
  if (error || !contained(root, canonical))
    throw std::runtime_error("PSF file escapes the selected folder");
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("Could not open PSF file for reading");
  const auto end = input.tellg();
  if (end <= 0) throw std::runtime_error("PSF file is empty or its size cannot be read");
  if (end > static_cast<std::streamoff>(psf::kMaxFileBytes))
    throw std::runtime_error("PSF file exceeds the 4 MiB limit");
  const auto count = static_cast<std::size_t>(end);
  if (count > available) throw std::runtime_error("PSF set exceeds the 64 MiB limit");
  std::vector<std::uint8_t> bytes(count);
  input.seekg(0, std::ios::beg);
  for (std::size_t at = 0; at < count;) {
    checkCancelled(cancelled);
    const auto amount = std::min<std::size_t>(65536, count - at);
    if (!input || !input.read(reinterpret_cast<char*>(bytes.data() + at), static_cast<std::streamsize>(amount)) ||
        input.gcount() != static_cast<std::streamsize>(amount))
      throw std::runtime_error("PSF file was truncated or could not be read completely");
    at += amount;
  }
  if (input.peek() != std::char_traits<char>::eof() || input.bad())
    throw std::runtime_error("PSF file changed or could not be read completely");
  const auto after = fs::symlink_status(path, error);
  if (error || fs::is_symlink(after) || !fs::is_regular_file(after))
    throw std::runtime_error("PSF file changed during reading");
  const auto afterSize = fs::file_size(path, error);
  if (error || afterSize != count) throw std::runtime_error("PSF file changed size during reading");
  const auto afterCanonical = fs::canonical(path, error);
  if (error || afterCanonical != canonical || !contained(root, afterCanonical))
    throw std::runtime_error("PSF file changed path during reading");
  checkCancelled(cancelled);
  return bytes;
}
std::shared_ptr<const PsfLoader::Document> readFolder(const fs::path& selected,
                                                     const Cancelled& cancelled) {
  checkCancelled(cancelled);
  const auto root = selectedRoot(selected);
  auto document = std::make_shared<PsfLoader::Document>();
  document->root = root.u8string();
  std::vector<fs::path> paths;
  std::size_t visited = 0;
  std::error_code error;
  fs::recursive_directory_iterator at(root, fs::directory_options::none, error), end;
  if (error) throw std::runtime_error("Could not read the selected PSF folder");
  for (; at != end; at.increment(error)) {
    if (error) throw std::runtime_error("Could not read a directory in the selected PSF folder");
    checkCancelled(cancelled);
    if (++visited > PsfLoader::kMaxTraversalEntries)
      throw std::runtime_error("PSF folder exceeds the 8192 directory-entry limit; choose a smaller folder");
    const auto status = at->symlink_status(error);
    if (error) throw std::runtime_error("Could not inspect a file in the selected PSF folder");
    if (fs::is_symlink(status)) { at.disable_recursion_pending(); continue; }
    if (!fs::is_regular_file(status) || !related(at->path())) continue;
    if (paths.size() == psf::kMaxFiles) throw std::runtime_error("PSF set exceeds the 512-file limit");
    paths.push_back(at->path());
  }
  if (error) throw std::runtime_error("Could not read a directory in the selected PSF folder");
  if (paths.empty()) throw std::runtime_error("No PSF1, MiniPSF or PSFLib files found in the selected folder");
  std::sort(paths.begin(), paths.end(), [&](const fs::path& a, const fs::path& b) {
    return a.lexically_relative(root).generic_u8string() < b.lexically_relative(root).generic_u8string();
  });
  std::size_t total = 0;
  for (const auto& path : paths) {
    checkCancelled(cancelled);
    auto bytes = readFile(root, path, psf::kMaxSetBytes - total, cancelled);
    total += bytes.size();
    document->files.push_back({path.lexically_relative(root).generic_u8string(), std::move(bytes)});
  }
  checkCancelled(cancelled);
  document->entries = psf::entries(document->files);
  checkCancelled(cancelled);
  return document;
}
}

PsfLoader::PsfLoader() : mWorker([this] { run(); }) {}
PsfLoader::~PsfLoader() {
  { std::lock_guard<std::mutex> lock(mMutex); mStopping = true; mJob.reset(); mPrepared.reset(); }
  mWake.notify_one();
  mWorker.join();
}
PsfLoader::Ticket PsfLoader::nextTicket() const {
  if (mStatus.ticket == std::numeric_limits<Ticket>::max())
    throw std::overflow_error("PSF staging ticket limit reached");
  return mStatus.ticket + 1;
}
PsfLoader::Ticket PsfLoader::requestFile(std::string path) {
  validInput(path, "Choose a PSF file");
  Job job; job.path = std::move(path); job.kind = Job::Kind::File;
  return request(std::move(job));
}
PsfLoader::Ticket PsfLoader::requestFolder(std::string path) {
  validInput(path, "Choose a PSF folder");
  Job job; job.path = std::move(path); job.kind = Job::Kind::Folder;
  return request(std::move(job));
}
PsfLoader::Ticket PsfLoader::requestEntry(std::shared_ptr<const Document> document, std::string entry) {
  if (!document) throw std::invalid_argument("Choose a PSF folder before choosing an entry");
  validInput(entry, "Choose a PSF entry");
  Job job; job.document = std::move(document); job.path = std::move(entry); job.kind = Job::Kind::Entry;
  return request(std::move(job));
}
PsfLoader::Ticket PsfLoader::request(Job job) {
  Ticket ticket;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    ticket = nextTicket(); job.ticket = ticket;
    mJob.emplace(std::move(job)); mPrepared.reset(); mStatus = {ticket, State::Reading, {}};
  }
  mWake.notify_one();
  return ticket;
}
void PsfLoader::cancel() {
  std::lock_guard<std::mutex> lock(mMutex);
  const auto ticket = nextTicket();
  mJob.reset(); mPrepared.reset(); mStatus = {ticket, State::Idle, {}};
}
PsfLoader::Status PsfLoader::status() const {
  std::lock_guard<std::mutex> lock(mMutex);
  return mStatus;
}
bool PsfLoader::current(Ticket ticket) const {
  std::lock_guard<std::mutex> lock(mMutex);
  return !mStopping && mStatus.ticket == ticket;
}
std::optional<PsfLoader::Prepared> PsfLoader::takeReady(Ticket ticket) {
  std::lock_guard<std::mutex> lock(mMutex);
  if (mStatus.ticket != ticket || mStatus.state != State::Ready || !mPrepared) return {};
  auto result = std::move(mPrepared); mPrepared.reset(); mStatus.state = State::Idle;
  return result;
}
void PsfLoader::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait(lock, [this] { return mStopping || mJob.has_value(); });
      if (mStopping) return;
      job = std::move(*mJob); mJob.reset();
    }
    const Cancelled cancelled = [this, ticket = job.ticket] { return !current(ticket); };
    std::optional<Prepared> prepared;
    std::string error;
    try {
      checkCancelled(cancelled);
      auto document = job.document;
      std::string entry = job.path;
      if (job.kind == Job::Kind::Entry) {
        // Only an immutable, valid catalog can survive an extraction failure.
        // Validation stays on this worker even for a caller-created Document.
        if (psf::entries(document->files) != document->entries)
          throw std::runtime_error("PSF document catalog does not match its files");
      } else {
        const auto selected = fs::u8path(job.path);
        if (job.kind == Job::Kind::File) {
          if (!related(selected)) throw std::runtime_error("Choose a PSF1, MiniPSF or PSFLib file");
          std::error_code fsError;
          const auto fileStatus = fs::symlink_status(selected, fsError);
          if (fsError || fs::is_symlink(fileStatus) || !fs::is_regular_file(fileStatus))
            throw std::runtime_error("PSF file is missing, is a symlink, or is not a regular file");
          auto parent = selected.parent_path();
          if (parent.empty()) parent = fs::path(".");
          document = readFolder(parent, cancelled);
          entry = selected.filename().generic_u8string();
        } else {
          document = readFolder(selected, cancelled);
          entry = document->entries.front();
        }
      }
      checkCancelled(cancelled);
      Prepared result; result.document = std::move(document);
      result.entryPath = entry;
      try {
        result.extraction = psf::extract(result.document->files, entry, cancelled);
      } catch (const std::exception& failure) {
        checkCancelled(cancelled);
        result.error = failure.what();
        if (job.kind == Job::Kind::File &&
            (result.error.find("missing PSFLib") != std::string::npos ||
             result.error.find("library path escapes") != std::string::npos))
          result.error += " Select a PSF folder containing this entry and all of its libraries when they are in a parent folder.";
      } catch (...) {
        checkCancelled(cancelled);
        result.error = "Unknown PSF extraction failure";
      }
      checkCancelled(cancelled);
      prepared = std::move(result);
    } catch (const std::exception& failure) { error = failure.what(); }
      catch (...) { error = "Unknown PSF loading failure"; }
    {
      std::lock_guard<std::mutex> lock(mMutex);
      if (mStopping) return;
      if (mStatus.ticket != job.ticket) continue;
      if (prepared) { mPrepared = std::move(prepared); mStatus.state = State::Ready; }
      else { mStatus.state = State::Failed; mStatus.error = std::move(error); }
    }
  }
}

} // namespace tetorica
