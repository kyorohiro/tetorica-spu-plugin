// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrumentExporter.h"
#include "SpuEngineExchange.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

namespace {
using namespace tetorica;
namespace fs=std::filesystem;
using Exporter=WebInstrumentExporter;
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string contents(const fs::path& path){std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot read test output");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
void put(const fs::path& path,const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;file.close();require(bool(file),"Cannot write original test file");}
struct Directory {
  fs::path path=fs::absolute("web-export-worker-test-"+std::to_string(Clock::now().time_since_epoch().count()));
  Directory(){require(fs::create_directory(path),"Cannot create private test directory");}
  ~Directory(){std::error_code ignored;fs::remove_all(path,ignored);}
};
Exporter::Status wait(Exporter& exporter,Exporter::Ticket ticket,Exporter::State expected) {
  const auto end=Clock::now()+10s;
  do {
    const auto status=exporter.status();require(status.ticket==ticket,"A stale worker replaced the latest export ticket");
    if(status.state==expected)return status;
    if(status.state==Exporter::State::Failed)throw std::runtime_error("Unexpected export failure: "+status.error);
    std::this_thread::sleep_for(1ms);
  }while(Clock::now()<end);
  throw std::runtime_error("Export worker timed out");
}
Exporter::Status failed(Exporter& exporter,Exporter::Ticket ticket) {
  const auto end=Clock::now()+10s;
  do {const auto s=exporter.status();require(s.ticket==ticket,"Failure status belongs to a stale ticket");
    if(s.state==Exporter::State::Failed){require(!s.error.empty(),"Export failure lost its reason");return s;}
    std::this_thread::sleep_for(1ms);
  }while(Clock::now()<end);
  throw std::runtime_error("Export failure timed out");
}
std::size_t temporaryCount(const fs::path& parent) {
  std::size_t count=0;for(const auto& entry:fs::directory_iterator(parent))
    if(entry.path().filename().u8string().find(".tetorica-spu-export-")==0)++count;
  return count;
}
SavedInstrumentState state() {
  SavedInstrumentState result;result.definition=InstrumentDefinition::builtin();result.gainPercent=37;
  return result;
}

void stagingAndLatest(const fs::path& parent) {
  Exporter exporter;const auto source=state();
  require(exporter.status().state==Exporter::State::Idle,"Exporter did not start Idle");
  const auto old=exporter.prepare(source,"First");wait(exporter,old,Exporter::State::Ready);
  const auto oldPreview=exporter.preview(old);require(oldPreview&&!oldPreview->notes.empty(),"Preview lost projection notes");
  Exporter::Ticket latest=old;
  for(unsigned n=0;n<16;++n)latest=exporter.prepare(source,"Latest "+std::to_string(n));
  const auto ready=wait(exporter,latest,Exporter::State::Ready);
  require(!exporter.preview(old)&&!exporter.save(old,(parent/"stale.json").u8string()),"Stale ticket saved a file");
  require(!fs::exists(parent/"stale.json"),"Stale save created an output");
  const auto path=parent/"latest.spu.json";
  require(exporter.save(latest,path.u8string()),"Current Ready export was not accepted");
  const auto saved=wait(exporter,latest,Exporter::State::Saved);
  require(saved.notes==ready.notes&&saved.path==path.u8string(),"Saved status lost notes or UTF-8 path");
  require(contents(path)==writeWebInstrument(source,"Latest 15").json,"Worker published an older JSON snapshot");
  require(!exporter.save(latest,path.u8string()),"A completed ticket was saved a second time");
  const auto cancelled=exporter.prepare(source,"Cancelled");wait(exporter,cancelled,Exporter::State::Ready);
  exporter.cancel();require(exporter.status().state==Exporter::State::Idle&&!exporter.preview(cancelled),"Cancel retained staged JSON");
  require(!exporter.save(cancelled,(parent/"cancelled.json").u8string()),"Cancelled preview remained saveable");
  const auto bad=exporter.prepare(source,"");failed(exporter,bad);
  auto invalid=source;invalid.gainPercent=std::numeric_limits<double>::quiet_NaN();
  failed(exporter,exporter.prepare(invalid,"Invalid gain"));
  require(temporaryCount(parent)==0,"Preparation/stale/cancel left a temporary directory");
}

void writeAndOverwrite(const fs::path& parent) {
  const auto folder=parent/fs::u8path(u8"音色 書き出し");require(fs::create_directory(folder),"Cannot create UTF-8 folder");
  const auto path=folder/fs::u8path(u8"波形 α.spu.json");const auto original=folder/"original-hardlink.txt";
  const std::string old="Original file must survive failed writes.\n";put(path,old);
  std::error_code linkError;fs::create_hard_link(path,original,linkError);
  const auto source=state();const auto expected=writeWebInstrument(source,u8"Export 音色");
  Exporter exporter;const auto ticket=exporter.prepare(source,u8"Export 音色");wait(exporter,ticket,Exporter::State::Ready);
  for(const auto& invalid:std::vector<std::string>{std::string{},std::string("bad\0name",8),std::string("\xc0\xaf",2)}) {
    bool rejected=false;try{exporter.save(ticket,invalid);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected&&exporter.status().state==Exporter::State::Ready,"Invalid path consumed the prepared snapshot");
  }
#ifndef _WIN32
  // Readers of the destination see complete old or complete new data, never a
  // partial JSON file. Windows readers may deny replacement while open, so the
  // cross-platform overwrite assertions below do not keep it open there.
  std::atomic<bool> stop{false},partial{false};std::promise<void> entered;
  std::thread reader([&]{bool first=true;while(!stop.load()) {
    try{const auto data=contents(path);if(data!=old&&data!=expected.json)partial=true;}
    catch(...){partial=true;}
    if(first){entered.set_value();first=false;}
  }});
  entered.get_future().wait();
  bool accepted=false;
  try{accepted=exporter.save(ticket,path.u8string());wait(exporter,ticket,Exporter::State::Saved);}
  catch(...){stop=true;reader.join();throw;}
  stop=true;reader.join();require(accepted&&!partial,"Destination exposed a partial export during atomic replacement");
#else
  require(exporter.save(ticket,path.u8string()),"UTF-8 Save was not accepted");wait(exporter,ticket,Exporter::State::Saved);
#endif
  require(contents(path)==expected.json,"UTF-8 overwrite did not write exact prepared JSON");
  if(!linkError)require(contents(original)==old,"Export truncated the old inode instead of atomically replacing it");
  require(temporaryCount(folder)==0,"Successful overwrite leaked a temporary directory");
  const auto failedPath=folder/"protected";require(fs::create_directory(failedPath),"Cannot create failure target");put(failedPath/"sentinel",old);
  const auto failTicket=exporter.prepare(source,"Fail directory");wait(exporter,failTicket,Exporter::State::Ready);
  require(exporter.save(failTicket,failedPath.u8string()),"Worker did not accept failure-path operation");failed(exporter,failTicket);
  require(contents(failedPath/"sentinel")==old&&temporaryCount(folder)==0,"Failed Save changed old contents or leaked temporary data");
}

#ifndef _WIN32
struct FileSizeLimit {
  rlimit original{};struct sigaction previous{};bool changed=false;
  FileSizeLimit(){require(!getrlimit(RLIMIT_FSIZE,&original),"Cannot read file-size limit");
    struct sigaction ignore{};ignore.sa_handler=SIG_IGN;sigemptyset(&ignore.sa_mask);
    require(!sigaction(SIGXFSZ,&ignore,&previous),"Cannot configure bounded write-failure test");
    auto limited=original;limited.rlim_cur=256;
    if(setrlimit(RLIMIT_FSIZE,&limited)){sigaction(SIGXFSZ,&previous,nullptr);throw std::runtime_error("Cannot set bounded file-size limit");}
    changed=true;}
  ~FileSizeLimit(){if(changed){setrlimit(RLIMIT_FSIZE,&original);sigaction(SIGXFSZ,&previous,nullptr);}}
};
void partialWriteFailure(const fs::path& parent) {
  const auto path=parent/"old-must-remain.spu.json";const std::string old="Keep existing preset unchanged\n";put(path,old);
  Exporter exporter;const auto ticket=exporter.prepare(state(),"Bounded write failure");wait(exporter,ticket,Exporter::State::Ready);
  {FileSizeLimit limit;require(exporter.save(ticket,path.u8string()),"Write-failure operation was not accepted");failed(exporter,ticket);}
  require(contents(path)==old&&temporaryCount(parent)==0,"Partial write destroyed old file or leaked its temporary file");
}
#endif

void acceptedWriteAndNonMutation(const fs::path& parent) {
  auto source=state();SpuEngineExchange exchange;
  const auto definition=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
  const auto generation=exchange.commitPrepared(prepareDefinition(*definition),definition);
  const auto snapshot=exchange.snapshotAuthoring();const auto* active=&exchange.instrument();
  const auto before=encodeState(source.definition,source.gainPercent,source.envelope,source.reverb,source.noise,source.pmon,source.morph);
  const auto path=parent/"accepted-survives-close.spu.json";const auto expected=writeWebInstrument(source,"Accepted before close");
  {Exporter exporter;const auto ticket=exporter.prepare(source,"Accepted before close");wait(exporter,ticket,Exporter::State::Ready);
    require(exporter.save(ticket,path.u8string()),"Accepted close-path Save failed");exporter.cancel();}
  require(contents(path)==expected.json,"Accepted Save was dropped when the editor/exporter closed");
  require(exchange.latestGeneration()==generation&&exchange.snapshotAuthoring().definition==snapshot.definition&&
    &exchange.instrument()==active&&exchange.adoptedGeneration()==0,"Export modified active or pending engine state");
  require(encodeState(source.definition,source.gainPercent,source.envelope,source.reverb,source.noise,source.pmon,source.morph)==before,
    "Export modified caller settings or source definition");
  require(temporaryCount(parent)==0,"Accepted write shutdown leaked temporary data");
}
}
int main(){try{Directory directory;stagingAndLatest(directory.path);writeAndOverwrite(directory.path);
#ifndef _WIN32
  partialWriteFailure(directory.path);
#endif
  acceptedWriteAndNonMutation(directory.path);
  std::cout<<"PASS Web export worker latestPreview=true staleCancel=true UTF8=true atomicOverwrite=true oldInodeRetained=true failedWritePreservesOld=true acceptedWriteSurvivesClose=true sourcePendingParamsUnchanged=true\n";
}catch(const std::exception& error){std::cerr<<"FAIL Web export worker: "<<error.what()<<'\n';return 1;}}
