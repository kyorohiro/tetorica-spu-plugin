// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "VabLoader.h"
#include "VabBank.h"
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace tetorica {
namespace {
// Standard VH contains the fixed128-Program table, at most128 packed Tone
// tables and one sample-size table. The split parser requires this exact size.
constexpr std::size_t kMaxSplitHeaderBytes=0x820+128*0x200+0x200;
std::vector<std::uint8_t> readBoundedFile(const std::string& utf8Path,
    std::size_t maximum=vab::kMaxFileBytes,const char* role="VAB",const char* limit="1 MiB") {
  if(utf8Path.empty()||utf8Path.find('\0')!=std::string::npos)
    throw std::runtime_error(std::string("Choose a ")+role+" file");
  const auto path=std::filesystem::u8path(utf8Path);
  std::error_code error;
  if(!std::filesystem::is_regular_file(path,error)||error)
    throw std::runtime_error(std::string(role)+" file is missing or is not a regular file");
  // Measure the opened file before allocating its input buffer. Check the
  // exact read and an EOF probe too, so shrink/growth during reading fails.
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  if(!input)throw std::runtime_error(std::string("Could not open ")+role+" file for reading");
  const auto end=input.tellg();
  if(end<=0)throw std::runtime_error(std::string(role)+" file is empty or its size cannot be read");
  if(end>static_cast<std::streamoff>(maximum))
    throw std::runtime_error(std::string(role)+" file exceeds the "+limit+" limit");
  const auto count=static_cast<std::size_t>(end);
  std::vector<std::uint8_t> bytes(count);
  input.seekg(0,std::ios::beg);
  if(!input||!input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(count))||
     input.gcount()!=static_cast<std::streamsize>(count))
    throw std::runtime_error(std::string(role)+" file was truncated or could not be read completely");
  if(input.peek()!=std::char_traits<char>::eof()||input.bad())
    throw std::runtime_error(std::string(role)+" file changed or could not be read completely");
  // A standard-library file buffer may have prefetched bytes before a shrink,
  // so a successful read alone is insufficient to notice a concurrent change.
  const auto after=std::filesystem::file_size(path,error);
  if(error||after!=count)throw std::runtime_error(std::string(role)+" file changed size during reading");
  return bytes;
}
constexpr const char* kSuperseded="VAB preparation was superseded by a newer instrument state";
MorphEndpoints retainedEndpoints(SpuEngineExchange& exchange) {
  const auto source=exchange.snapshotDefinition();
  return source?source->morphEndpoints():MorphEndpoints{};
}
}

VabLoader::VabLoader(SpuEngineExchange& exchange)
  : mExchange(exchange),mWorker([this]{run();}) {}

VabLoader::~VabLoader() {
  {
    std::lock_guard<std::mutex> lock(mMutex);
    mStopping=true;
    mJob.reset();
  }
  mWake.notify_all();
  mWorker.join();
  mExchange.collectRetired();
}

VabLoader::Generation VabLoader::request(std::vector<std::uint8_t> complete,std::size_t flatTone) {
  Generation generation;
  {
    // Serialize generation issuance and job replacement together. Separate
    // locks would allow concurrent request calls to queue the older job last.
    std::lock_guard<std::mutex> lock(mMutex);
    const auto endpoints=retainedEndpoints(mExchange);
    generation=mExchange.beginRequest();
    mJob.reset();
    if(complete.size()>vab::kMaxFileBytes) {
      mStatus={generation,State::Failed,"VAB input exceeds the 1 MiB limit"};
    } else {
      mStatus={generation,State::Loading,{}};
      Job job;job.generation=generation;job.bytes=std::move(complete);job.flatTone=flatTone;
      job.endpoints=endpoints;
      mJob.emplace(std::move(job));
    }
  }
  mWake.notify_one();
  return generation;
}

VabLoader::Generation VabLoader::requestFile(std::string path) {
  return requestFileImpl(std::move(path),0,true);
}
VabLoader::Generation VabLoader::requestFile(std::string path,std::size_t flatTone) {
  return requestFileImpl(std::move(path),flatTone,false);
}
VabLoader::Generation VabLoader::requestFileImpl(std::string path,std::size_t flatTone,bool firstPlayable) {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto endpoints=retainedEndpoints(mExchange);
    generation=mExchange.beginRequest();
    Job job;job.generation=generation;job.kind=Job::Kind::File;
    job.path=std::move(path);job.flatTone=flatTone;job.firstPlayable=firstPlayable;
    job.endpoints=endpoints;
    mJob.emplace(std::move(job));
    mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Generation VabLoader::requestSplitFiles(std::string headerPath,std::string bodyPath) {
  return requestSplitFilesImpl(std::move(headerPath),std::move(bodyPath),0,true);
}
VabLoader::Generation VabLoader::requestSplitFiles(
    std::string headerPath,std::string bodyPath,std::size_t flatTone) {
  return requestSplitFilesImpl(std::move(headerPath),std::move(bodyPath),flatTone,false);
}
VabLoader::Generation VabLoader::requestSplitFilesImpl(
    std::string headerPath,std::string bodyPath,std::size_t flatTone,bool firstPlayable) {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto endpoints=retainedEndpoints(mExchange);
    generation=mExchange.beginRequest();
    Job job;job.generation=generation;job.kind=Job::Kind::SplitFiles;
    job.path=std::move(headerPath);job.bodyPath=std::move(bodyPath);
    job.flatTone=flatTone;job.firstPlayable=firstPlayable;job.endpoints=endpoints;
    mJob.emplace(std::move(job));mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Generation VabLoader::requestSelection(std::shared_ptr<const InstrumentDefinition> source,
                                                  std::uint32_t program,std::uint32_t tone) {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    generation=mExchange.beginRequest();
    Job job;job.generation=generation;job.kind=Job::Kind::Selection;
    if(source)job.endpoints=source->morphEndpoints();
    job.source=std::move(source);job.program=program;job.tone=tone;
    mJob.emplace(std::move(job));mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Generation VabLoader::requestProgram(std::shared_ptr<const InstrumentDefinition> source,
                                                std::uint32_t program) {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    generation=mExchange.beginRequest();
    Job job;job.generation=generation;job.kind=Job::Kind::Program;
    if(source)job.endpoints=source->morphEndpoints();
    job.source=std::move(source);job.program=program;job.firstPlayable=true;
    mJob.emplace(std::move(job));mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Generation VabLoader::requestBuiltin() {
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto endpoints=retainedEndpoints(mExchange);
    generation=mExchange.beginRequest();
    mStatus={generation,State::Loading,{}};
    Job job;job.generation=generation;job.kind=Job::Kind::Builtin;
    job.endpoints=endpoints;
    mJob.emplace(std::move(job));
  }
  mWake.notify_one();
  return generation;
}

VabLoader::Generation VabLoader::requestPcmCycle(const PcmCycle& cycle) {
  validatePcmCycle(cycle);
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto endpoints=retainedEndpoints(mExchange);
    generation=mExchange.beginRequest();
    Job job;job.generation=generation;job.kind=Job::Kind::PcmCycle;job.cycle=cycle;
    job.endpoints=endpoints;
    mJob.emplace(std::move(job));mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Generation VabLoader::requestMorphEndpoints(
    std::shared_ptr<const InstrumentDefinition> source,const MorphEndpoints& endpoints) {
  if(!source)throw std::invalid_argument("Choose a source before applying Morph endpoints");
  validateMorphEndpoints(endpoints);
  Job job;job.kind=Job::Kind::MorphEndpoints;job.source=std::move(source);job.endpoints=endpoints;
  Generation generation;
  {
    std::lock_guard<std::mutex> lock(mMutex);
    generation=mExchange.beginRequest();job.generation=generation;
    mJob.emplace(std::move(job));mStatus={generation,State::Loading,{}};
  }
  mWake.notify_one();return generation;
}

VabLoader::Status VabLoader::status() const {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStatus.state==State::Loading&&mStatus.generation!=mExchange.latestGeneration())
    return {mStatus.generation,State::Failed,kSuperseded};
  return mStatus;
}

bool VabLoader::current(Generation generation) const {
  std::lock_guard<std::mutex> lock(mMutex);
  return !mStopping&&mStatus.generation==generation&&mExchange.latestGeneration()==generation;
}

void VabLoader::finish(Generation generation,std::unique_ptr<SpuInstrument>,std::string error) {
  std::lock_guard<std::mutex> lock(mMutex);
  if(mStopping || mStatus.generation!=generation)return;
  // Success publishes engine+definition together in run(); this helper handles
  // preparation exceptions without changing active/pending authoring state.
  mStatus={generation,State::Failed,error.empty()?"Unknown VAB preparation failure":std::move(error)};
}

void VabLoader::run() {
  for(;;) {
    std::optional<Job> job;
    {
      std::unique_lock<std::mutex> lock(mMutex);
      mWake.wait_for(lock,std::chrono::milliseconds(20),[this]{return mStopping||mJob.has_value();});
      if(mStopping)return;
      job=std::move(mJob);
      mJob.reset();
    }
    // Audio never notifies a condition variable. Periodic collection also
    // reclaims engines when no new file request arrives.
    mExchange.collectRetired();
    if(!job)continue;
    if(!current(job->generation)){finish(job->generation,nullptr,kSuperseded);continue;}
    try {
      std::shared_ptr<const InstrumentDefinition> definition;
      std::unique_ptr<SpuInstrument> prepared;
      if(job->kind==Job::Kind::File)job->bytes=readBoundedFile(job->path);
      if(job->kind==Job::Kind::SplitFiles) {
        auto header=readBoundedFile(job->path,kMaxSplitHeaderBytes,"VH header","68,128-byte");
        if(!current(job->generation)){finish(job->generation,nullptr,kSuperseded);continue;}
        auto body=readBoundedFile(job->bodyPath,vab::kMaxBodyBytes,"VB sample body","512 KiB");
        if(header.size()>vab::kMaxFileBytes-body.size())
          throw std::runtime_error("VH + VB input exceeds the 1 MiB VAB limit");
        // Validate split boundaries before concatenation: a complete VAB used
        // as VH must not accidentally pass the ordinary complete-file parser.
        // Header fields, reserved prefix, sample padding and packet flags are
        // retained unchanged, rather than normalized or reconstructed.
        (void)vab::parse({header.data(),header.size()},{body.data(),body.size()});
        header.insert(header.end(),body.begin(),body.end());
        job->bytes=std::move(header);
      }
      if(job->kind==Job::Kind::Program){
        if(!job->source||job->source->kind()!=InstrumentDefinition::Kind::Vab||job->program>127)
          throw std::runtime_error("Choose a loaded VAB Program");
        job->bytes=job->source->bankBytes();
      }
      if(!current(job->generation)){finish(job->generation,nullptr,kSuperseded);continue;}
      if(job->kind==Job::Kind::Builtin)definition=InstrumentDefinition::builtin();
      else if(job->kind==Job::Kind::PcmCycle)definition=InstrumentDefinition::fromPcmCycle(job->cycle);
      else if(job->kind==Job::Kind::MorphEndpoints)
        definition=InstrumentDefinition::withMorphEndpoints(job->source,job->endpoints);
      else if(job->kind==Job::Kind::Selection) {
        if(!job->source||job->source->kind()!=InstrumentDefinition::Kind::Vab)
          throw std::runtime_error("Choose a loaded VAB bank before selecting a Tone");
        definition=InstrumentDefinition::fromVab(job->source->bankBytes(),job->program,job->tone);
      } else if(job->firstPlayable) {
        const auto bank=vab::parse({job->bytes.data(),job->bytes.size()});
        std::string lastError;
        for(std::size_t i=0;i<bank.tones.size();++i) {
          if(job->kind==Job::Kind::Program&&bank.tones[i].program!=job->program)continue;
          if(!current(job->generation))break;
          try {
            prepared=std::make_unique<SpuInstrument>(vab::prepareTone(bank,i));
            definition=InstrumentDefinition::fromVabFlatIndex(std::move(job->bytes),i);
            break;
          } catch(const std::bad_alloc&) { throw; }
          catch(const std::exception& error) { lastError=error.what(); }
        }
        if(!current(job->generation)){finish(job->generation,nullptr,kSuperseded);continue;}
        if(!definition)throw std::runtime_error(job->kind==Job::Kind::Program?
            "No playable Tone found in VAB Program: "+lastError:"No playable Tone found in VAB bank: "+lastError);
      } else definition=InstrumentDefinition::fromVabFlatIndex(std::move(job->bytes),job->flatTone);
      if(!current(job->generation)){finish(job->generation,nullptr,kSuperseded);continue;}
      definition=InstrumentDefinition::withMorphEndpoints(definition,job->endpoints);
      if(!prepared)prepared=prepareDefinition(*definition);
      else if(!prepared->setMorphEndpoints(definition->morphEndpoints()))
        throw std::runtime_error("Could not prepare Morph endpoints");
      // Pair the immutable authoring definition with its prepared engine in the
      // same publication. No definition shared_ptr is copied by the audio path.
      std::lock_guard<std::mutex> lock(mMutex);
      if(mStopping||mStatus.generation!=job->generation)continue;
      if(mExchange.publish(job->generation,std::move(prepared),std::move(definition)))
        mStatus={job->generation,State::Ready,{}};
      else mStatus={job->generation,State::Failed,"SPU preparation was superseded before publication"};
    } catch(const std::exception& error) {
      finish(job->generation,nullptr,error.what());
    } catch(...) {
      finish(job->generation,nullptr,"Unknown VAB preparation failure");
    }
  }
}

} // namespace tetorica
