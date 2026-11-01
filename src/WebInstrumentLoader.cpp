// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrumentLoader.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace tetorica {
namespace {
std::string readFile(const std::string& path) {
  std::ifstream stream(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
  if(!stream)throw std::runtime_error("Could not open instrument file");
  const auto size=stream.tellg();
  if(size<=0||static_cast<std::uintmax_t>(size)>kMaxWebInstrumentBytes)
    throw std::runtime_error("Instrument JSON must be 1 byte to 48 MiB");
  std::string text(static_cast<std::size_t>(size),'\0');
  stream.seekg(0);
  if(!stream.read(text.data(),static_cast<std::streamsize>(text.size())))
    throw std::runtime_error("Could not read complete instrument file");
  if(stream.peek()!=std::char_traits<char>::eof())
    throw std::runtime_error("Instrument file changed during reading");
  return text;
}
}
WebInstrumentLoader::WebInstrumentLoader():mWorker([this]{run();}) {}
WebInstrumentLoader::~WebInstrumentLoader() {
  {std::lock_guard<std::mutex> lock(mMutex);mStopping=true;mJob.reset();}
  mWake.notify_one();
  mWorker.join();
}
WebInstrumentLoader::Ticket WebInstrumentLoader::requestFile(std::string path) {
  if(path.empty()||path.find('\0')!=std::string::npos)
    throw std::invalid_argument("Invalid instrument path");
  return request(std::move(path),true);
}
WebInstrumentLoader::Ticket WebInstrumentLoader::requestText(std::string text) {
  if(text.empty()||text.size()>kMaxWebInstrumentBytes)
    throw std::invalid_argument("Instrument JSON must be 1 byte to 48 MiB");
  return request(std::move(text),false);
}
WebInstrumentLoader::Ticket WebInstrumentLoader::request(std::string input,bool file) {
  Ticket ticket;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    ticket=mStatus.ticket+1;
    mJob=Job{ticket,std::move(input),file};
    mPrepared.reset();mStatus={ticket,State::Loading,{}};
  }
  mWake.notify_one();return ticket;
}
void WebInstrumentLoader::cancel() {
  std::lock_guard<std::mutex> lock(mMutex);
  mJob.reset();mPrepared.reset();mStatus={mStatus.ticket+1,State::Idle,{}};
}
WebInstrumentLoader::Status WebInstrumentLoader::status() const {
  std::lock_guard<std::mutex> lock(mMutex);return mStatus;
}
std::optional<WebInstrumentLoader::Prepared> WebInstrumentLoader::takeReady(Ticket ticket) {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStatus.ticket!=ticket||mStatus.state!=State::Ready||!mPrepared)return {};
  auto result=std::move(mPrepared);mPrepared.reset();
  mStatus.state=State::Idle;return result;
}
void WebInstrumentLoader::run() {
  for(;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait(lock,[this]{return mStopping||mJob.has_value();});
      if(mStopping)return;
      job=std::move(*mJob);mJob.reset();
    }
    std::optional<Prepared> result;
    std::string error;
    try {
      std::string fileText;
      if(job.file)fileText=readFile(job.input);
      auto imported=readWebInstrument(job.file?std::string_view(fileText):std::string_view(job.input));
      // The immutable decoded PCM no longer needs the complete input JSON.
      std::string().swap(fileText);std::string().swap(job.input);
      auto engine=prepareDefinition(*imported.state.definition);
      result=Prepared{job.ticket,std::move(imported),std::move(engine)};
    } catch(const std::exception& failure) {error=failure.what();}
      catch(...) {error="Unknown instrument preparation failure";}
    {
      std::lock_guard<std::mutex> lock(mMutex);
      if(mStopping)return;
      if(mStatus.ticket!=job.ticket)continue;
      if(result) {mPrepared=std::move(result);mStatus.state=State::Ready;}
      else {mStatus.state=State::Failed;mStatus.error=std::move(error);}
    }
  }
}
} // namespace tetorica
