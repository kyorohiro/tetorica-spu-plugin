// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrumentExporter.h"
#include "WebInstrument.h"
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tetorica {
namespace {
namespace fs=std::filesystem;
void validatePath(const std::string& path) {
  if(path.empty()||path.find('\0')!=std::string::npos)
    throw std::invalid_argument("Choose a valid export filename");
  // File dialogs supply UTF-8. Reject malformed spelling before accepting a
  // save, rather than using a different name on different native platforms.
  for(std::size_t at=0;at<path.size();) {
    const unsigned first=static_cast<unsigned char>(path[at++]);
    unsigned tail=0;std::uint32_t point=first,minimum=0;
    if(first<0x80)continue;
    if(first>=0xc2&&first<=0xdf){tail=1;point=first&31;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){tail=2;point=first&15;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){tail=3;point=first&7;minimum=0x10000;}
    else throw std::invalid_argument("Export path must be valid UTF-8");
    if(tail>path.size()-at)throw std::invalid_argument("Export path must be valid UTF-8");
    for(unsigned n=0;n<tail;++n){const unsigned byte=static_cast<unsigned char>(path[at++]);
      if((byte&0xc0)!=0x80)throw std::invalid_argument("Export path must be valid UTF-8");
      point=(point<<6)|(byte&63);
    }
    if(point<minimum||point>0x10ffff||(point>=0xd800&&point<=0xdfff))
      throw std::invalid_argument("Export path must be valid UTF-8");
  }
  const auto native=fs::u8path(path);
  if(native.filename().empty()||native.filename()=="."||native.filename()=="..")
    throw std::invalid_argument("Choose an export filename, not a directory");
}
struct TempDirectory {
  fs::path path;
  explicit TempDirectory(const fs::path& parent) {
#ifdef _WIN32
    static std::atomic<std::uint64_t> sequence{0};
    const auto clock=std::chrono::steady_clock::now().time_since_epoch().count();
    for(unsigned attempt=0;attempt<32;++attempt) {
      const auto name=".tetorica-spu-export-"+std::to_string(GetCurrentProcessId())+"-"+
        std::to_string(clock)+"-"+std::to_string(sequence.fetch_add(1));
      std::error_code error;auto candidate=parent/fs::u8path(name);
      if(fs::create_directory(candidate,error)){path=std::move(candidate);return;}
      if(error&&error!=std::errc::file_exists)throw std::system_error(error,"Could not create export temporary directory");
    }
    throw std::runtime_error("Could not choose a unique export temporary directory");
#else
    auto pattern=(parent/".tetorica-spu-export-XXXXXX").native();
    std::vector<char> owned(pattern.begin(),pattern.end());owned.push_back('\0');
    const auto created=::mkdtemp(owned.data());
    if(!created)throw std::system_error(errno,std::generic_category(),"Could not create export temporary directory");
    try{path=fs::path(created);}catch(...){::rmdir(created);throw;}
    // mkdtemp creates this directory privately (0700).
#endif
  }
  ~TempDirectory(){std::error_code ignored;fs::remove_all(path,ignored);}
  TempDirectory(const TempDirectory&)=delete;
  TempDirectory& operator=(const TempDirectory&)=delete;
};

void writeAtomically(const std::string& utf8Path,const std::string& json) {
  validatePath(utf8Path);
  if(json.empty()||json.size()>kMaxWebInstrumentBytes)
    throw std::runtime_error("Prepared instrument JSON exceeds its bounded size");
  const auto destination=fs::absolute(fs::u8path(utf8Path));
  const auto parent=destination.parent_path();
  std::error_code error;
  const auto oldStatus=fs::status(destination,error);
  if(!error&&fs::is_directory(oldStatus))throw std::runtime_error("Export destination is a directory");
  TempDirectory temporary(parent);const auto file=temporary.path/"instrument.json";
#ifdef _WIN32
  HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(handle==INVALID_HANDLE_VALUE)throw std::system_error(GetLastError(),std::system_category(),"Could not create export temporary file");
  bool closed=false;
  try {
    std::size_t at=0;
    while(at<json.size()) {
      DWORD written=0;const auto count=static_cast<DWORD>(std::min<std::size_t>(json.size()-at,1u<<20));
      if(!WriteFile(handle,json.data()+at,count,&written,nullptr)||!written)
        throw std::system_error(GetLastError(),std::system_category(),"Could not write complete export file");
      at+=written;
    }
    if(!FlushFileBuffers(handle))throw std::system_error(GetLastError(),std::system_category(),"Could not flush export file");
    if(!CloseHandle(handle)){closed=true;throw std::system_error(GetLastError(),std::system_category(),"Could not close export file");}
    closed=true;
    if(!MoveFileExW(file.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
      throw std::system_error(GetLastError(),std::system_category(),"Could not replace export destination");
  }catch(...){if(!closed)CloseHandle(handle);throw;}
#else
  const int handle=::open(file.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
  if(handle<0)throw std::system_error(errno,std::generic_category(),"Could not create export temporary file");
  bool closed=false;
  try {
    // Replacing a private existing preset must not broaden its permissions.
    if(!error&&fs::is_regular_file(oldStatus))
      if(::fchmod(handle,static_cast<mode_t>(oldStatus.permissions())&0777))
        throw std::system_error(errno,std::generic_category(),"Could not retain export file permissions");
    std::size_t at=0;
    while(at<json.size()) {
      const auto written=::write(handle,json.data()+at,json.size()-at);
      if(written<0&&errno==EINTR)continue;
      if(written<=0)throw std::system_error(errno,std::generic_category(),"Could not write complete export file");
      at+=static_cast<std::size_t>(written);
    }
    if(::fsync(handle))throw std::system_error(errno,std::generic_category(),"Could not flush export file");
    if(::close(handle)){closed=true;throw std::system_error(errno,std::generic_category(),"Could not close export file");}
    closed=true;
    if(::rename(file.c_str(),destination.c_str()))
      throw std::system_error(errno,std::generic_category(),"Could not replace export destination");
  }catch(...){if(!closed)::close(handle);throw;}
#endif
}
}

WebInstrumentExporter::WebInstrumentExporter():mWorker([this]{run();}) {}
WebInstrumentExporter::~WebInstrumentExporter() {
  {std::lock_guard<std::mutex> lock(mMutex);mStopping=true;mPrepared.reset();
    if(mJob&&mJob->kind==Job::Kind::Prepare)mJob.reset();}
  mWake.notify_one();mWorker.join();
}
WebInstrumentExporter::Ticket WebInstrumentExporter::nextTicketLocked() {
  if(mStatus.ticket==std::numeric_limits<Ticket>::max())
    throw std::overflow_error("Export ticket counter exhausted");
  return mStatus.ticket+1;
}
WebInstrumentExporter::Ticket WebInstrumentExporter::prepare(SavedInstrumentState state,std::string name) {
  Ticket ticket;
  {std::lock_guard<std::mutex> lock(mMutex);
    if(mWriting)throw std::runtime_error("An accepted export is still being written");
    ticket=nextTicketLocked();Job job;job.ticket=ticket;job.state=std::move(state);job.text=std::move(name);
    mJob=std::move(job);mPrepared.reset();mStatus={ticket,State::Preparing,{},{},{}};}
  mWake.notify_one();return ticket;
}
WebInstrumentExporter::Status WebInstrumentExporter::status() const {
  std::lock_guard<std::mutex> lock(mMutex);return mStatus;
}
std::optional<WebInstrumentExporter::Preview> WebInstrumentExporter::preview(Ticket ticket) const {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStatus.ticket!=ticket||mStatus.state!=State::Ready||!mPrepared)return {};
  return Preview{ticket,mPrepared->notes};
}
bool WebInstrumentExporter::requestSave(Ticket ticket,std::string utf8Path) {
  validatePath(utf8Path);
  {std::lock_guard<std::mutex> lock(mMutex);
    if(mStatus.ticket!=ticket||mStatus.state!=State::Ready||!mPrepared)return false;
    // Allocate/copy the small status path before transferring prepared JSON.
    Status status{ticket,State::Writing,{},mPrepared->notes,utf8Path};
    Job job;job.kind=Job::Kind::Write;job.ticket=ticket;job.text=std::move(utf8Path);
    job.exported=std::move(mPrepared);mPrepared.reset();mJob=std::move(job);
    mWriting=true;mStatus=std::move(status);}
  mWake.notify_one();return true;
}
void WebInstrumentExporter::cancel() {
  std::lock_guard<std::mutex> lock(mMutex);
  const auto ticket=nextTicketLocked();
  if(mJob&&mJob->kind==Job::Kind::Prepare)mJob.reset();
  mPrepared.reset();mStatus={ticket,State::Idle,{},{},{}};
}
void WebInstrumentExporter::run() {
  for(;;) {
    Job job;
    {std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait(lock,[this]{return mStopping||mJob.has_value();});
      if(mStopping&&(!mJob||mJob->kind!=Job::Kind::Write))return;
      job=std::move(*mJob);mJob.reset();}
    std::optional<ExportedWebInstrument> result;std::string error;std::vector<std::string> notes;
    try {
      if(job.kind==Job::Kind::Prepare){result=writeWebInstrument(job.state,job.text);notes=result->notes;}
      else writeAtomically(job.text,job.exported->json);
    }catch(const std::exception& failure){error=failure.what();}
      catch(...){error="Unknown instrument export failure";}
    {std::lock_guard<std::mutex> lock(mMutex);
      if(job.kind==Job::Kind::Write)mWriting=false;
      if(mStopping||mStatus.ticket!=job.ticket)continue;
      if(!error.empty()){mStatus.state=State::Failed;mStatus.error=std::move(error);}
      else if(job.kind==Job::Kind::Prepare){mStatus.notes=std::move(notes);mPrepared=std::move(result);mStatus.state=State::Ready;}
      else mStatus.state=State::Saved;}
  }
}
} // namespace tetorica
