// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "AudioFileLoader.h"
#include "WavImport.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tetorica {
namespace {
std::vector<std::uint8_t> readFile(const std::string& utf8Path) {
  const auto path=std::filesystem::u8path(utf8Path);
  std::error_code error;
  if(!std::filesystem::is_regular_file(path,error)||error)
    throw std::runtime_error("WAV file is missing or is not a regular file");
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  if(!input)throw std::runtime_error("Could not open WAV file for reading");
  const auto end=input.tellg();
  if(end<=0)throw std::runtime_error("WAV file is empty or its size cannot be read");
  if(end>static_cast<std::streamoff>(kMaxWavFileBytes))
    throw std::runtime_error("WAV file exceeds the 32 MiB limit");
  const auto count=static_cast<std::size_t>(end);
  std::vector<std::uint8_t> bytes(count);
  input.seekg(0,std::ios::beg);
  if(!input||!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(count))||
     input.gcount()!=static_cast<std::streamsize>(count))
    throw std::runtime_error("WAV file was truncated or could not be read completely");
  if(input.peek()!=std::char_traits<char>::eof()||input.bad())
    throw std::runtime_error("WAV file changed or could not be read completely");
  // std::filebuf can prefetch data before a shrink; confirm the measured file
  // size as well as checking complete read and EOF to catch shrink or growth.
  const auto after=std::filesystem::file_size(path,error);
  if(error||after!=count)throw std::runtime_error("WAV file changed size during reading");
  return bytes;
}
bool whitespace(std::uint32_t code) {
  return (code>=9&&code<=13)||code==0x20||code==0xa0||code==0x1680||
    (code>=0x2000&&code<=0x200a)||code==0x2028||code==0x2029||
    code==0x202f||code==0x205f||code==0x3000||code==0xfeff;
}
// Strict Unicode decoding also supplies byte boundaries, so truncating a
// UTF-16-unit name never cuts a UTF-8 sequence or a supplementary character.
std::string importName(const std::string& path) {
  const auto text=std::filesystem::u8path(path).stem().u8string();
  struct Point {std::size_t begin,end;std::uint32_t code;};
  std::vector<Point> points;
  for(std::size_t at=0;at<text.size();) {
    const auto begin=at;const auto first=static_cast<unsigned char>(text[at++]);
    std::uint32_t code=first,minimum=0;unsigned count=0;
    if(first<0x80){if(!first)throw std::invalid_argument("WAV filename contains NUL");}
    else if(first>=0xc2&&first<=0xdf){count=1;code=first&31;minimum=0x80;}
    else if(first>=0xe0&&first<=0xef){count=2;code=first&15;minimum=0x800;}
    else if(first>=0xf0&&first<=0xf4){count=3;code=first&7;minimum=0x10000;}
    else throw std::invalid_argument("WAV filename must be valid UTF-8");
    if(count>text.size()-at)throw std::invalid_argument("WAV filename must be valid UTF-8");
    while(count--) {
      const auto c=static_cast<unsigned char>(text[at++]);
      if((c&0xc0)!=0x80)throw std::invalid_argument("WAV filename must be valid UTF-8");
      code=(code<<6)|(c&63);
    }
    if(code<minimum||code>0x10ffff||(code>=0xd800&&code<=0xdfff))
      throw std::invalid_argument("WAV filename must be valid UTF-8");
    points.push_back({begin,at,code});
  }
  std::size_t begin=0,end=points.size();
  while(begin<end&&whitespace(points[begin].code))++begin;
  while(end>begin&&whitespace(points[end-1].code))--end;
  unsigned units=0;std::size_t accepted=begin;
  while(accepted<end) {
    const unsigned length=points[accepted].code>0xffff?2:1;
    if(units+length>80)break;
    units+=length;++accepted;
  }
  // Truncation may expose trailing whitespace; return a writer-compatible name.
  while(accepted>begin&&whitespace(points[accepted-1].code))--accepted;
  if(accepted==begin)return "Imported WAV";
  return text.substr(points[begin].begin,points[accepted-1].end-points[begin].begin);
}
std::string describe(const WavAudioImportResult& result) {
  std::ostringstream text;
  if(result.softwareAudio())text<<"Software audio · ";
  text<<"WAV "<<result.sampleRate<<" Hz "<<(result.channels==1?"mono":"stereo")
      <<": "<<result.sourceFrames<<" source frames -> "<<result.resampledFrames
      <<" at 44100 Hz, ";
  if(result.softwareAudio())text<<double(result.resampledFrames)/44100.<<" seconds, mono PCM16 outside SPU RAM";
  else text<<"+"<<result.paddedFrames<<" padding";
  text<<"; C4 / one shot";
  return text.str();
}
}
AudioFileLoader::AudioFileLoader():mWorker([this]{run();}) {}
AudioFileLoader::~AudioFileLoader() {
  {std::lock_guard<std::mutex> lock(mMutex);mStopping=true;mJob.reset();}
  mWake.notify_one();mWorker.join();
}
AudioFileLoader::Ticket AudioFileLoader::nextTicket() const {
  if(mStatus.ticket==std::numeric_limits<Ticket>::max())
    throw std::overflow_error("WAV staging ticket limit reached");
  return mStatus.ticket+1;
}
AudioFileLoader::Ticket AudioFileLoader::requestFile(std::string path,MorphEndpoints endpoints) {
  if(path.empty()||path.find('\0')!=std::string::npos)throw std::invalid_argument("Choose a WAV file");
  validateMorphEndpoints(endpoints);
  Job job;job.path=std::move(path);job.endpoints=endpoints;job.file=true;
  return request(std::move(job));
}
AudioFileLoader::Ticket AudioFileLoader::requestDefinition(std::shared_ptr<const InstrumentDefinition> definition) {
  if(!definition)throw std::invalid_argument("Choose a sample before changing playback metadata");
  Job job;job.definition=std::move(definition);
  return request(std::move(job));
}
AudioFileLoader::Ticket AudioFileLoader::request(Job job) {
  Ticket ticket;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    ticket=nextTicket();job.ticket=ticket;
    mJob.emplace(std::move(job));mPrepared.reset();mStatus={ticket,State::Loading,{}};
  }
  mWake.notify_one();return ticket;
}
void AudioFileLoader::cancel() {
  std::lock_guard<std::mutex> lock(mMutex);
  const auto ticket=nextTicket();
  mJob.reset();mPrepared.reset();mStatus={ticket,State::Idle,{}};
}
AudioFileLoader::Status AudioFileLoader::status() const {
  std::lock_guard<std::mutex> lock(mMutex);return mStatus;
}
bool AudioFileLoader::current(Ticket ticket) const {
  std::lock_guard<std::mutex> lock(mMutex);return !mStopping&&mStatus.ticket==ticket;
}
std::optional<AudioFileLoader::Prepared> AudioFileLoader::takeReady(Ticket ticket) {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStatus.ticket!=ticket||mStatus.state!=State::Ready||!mPrepared)return {};
  auto result=std::move(mPrepared);mPrepared.reset();mStatus.state=State::Idle;
  return result;
}
void AudioFileLoader::run() {
  for(;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait(lock,[this]{return mStopping||mJob.has_value();});
      if(mStopping)return;
      job=std::move(*mJob);mJob.reset();
    }
    std::optional<Prepared> result;std::string error;
    try {
      auto definition=job.definition;std::string details="Playback metadata prepared";
      if(job.file) {
        const auto bytes=readFile(job.path);
        if(!current(job.ticket))continue;
        auto imported=importWavAudio({bytes.data(),bytes.size()},importName(job.path));
        if(!current(job.ticket))continue;
        details=describe(imported);
        auto source=imported.softwareAudio()?
          InstrumentDefinition::fromSoftwarePcmSample(std::get<SoftwarePcmSample>(imported.sample)):
          InstrumentDefinition::fromAdpcmSample(std::get<RawAdpcmSample>(imported.sample));
        definition=InstrumentDefinition::withMorphEndpoints(source,job.endpoints);
      }
      if(!current(job.ticket))continue;
      auto engine=prepareDefinition(*definition);
      result=Prepared{job.ticket,std::move(definition),std::move(engine),job.file,std::move(details)};
    } catch(const std::exception& failure) {error=failure.what();}
      catch(...) {error="Unknown WAV preparation failure";}
    {
      std::lock_guard<std::mutex> lock(mMutex);
      if(mStopping)return;
      if(mStatus.ticket!=job.ticket)continue;
      if(result){mPrepared=std::move(result);mStatus.state=State::Ready;}
      else {mStatus.state=State::Failed;mStatus.error=std::move(error);}
    }
  }
}
} // namespace tetorica
