// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Ownership/worker tests use actual primed SPUs and compare PCM, not just IDs.
#include "SpuEngineExchange.h"
#include "SpuInstrument.h"
#include "VabBank.h"
#include "VabLoader.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <future>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef VAB_FIXTURE_DIR
#define VAB_FIXTURE_DIR "tests/fixtures/vab"
#endif

namespace {
using Engine = tetorica::SpuInstrument;
using Exchange = tetorica::SpuEngineExchange;
using Loader = tetorica::VabLoader;
using Tone = tetorica::vab::PreparedTone;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
thread_local bool rtTracking = false;
thread_local std::size_t rtNew = 0, rtDelete = 0;
std::atomic<std::size_t> checks{0}, probes{0}, engineAllocations{0}, engineFrees{0};
std::atomic<std::size_t> engineRtFrees{0};
struct Allocation {
  std::atomic<void*> pointer{nullptr};
  std::atomic<bool> live{false};
};
std::array<Allocation,4096> allocations{};

// This test-only allocator gate gives a deterministic worker-in-construction
// boundary without modifying production scheduling or adding production hooks.
std::mutex gateMutex;
std::condition_variable gateWake;
std::atomic<bool> gateEnabled{false};
std::atomic<std::size_t> gateSize{sizeof(Engine)};
bool gateEntered = false, gateReleased = false;

[[noreturn]] void fail(const char* expression,int line) {
  std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::abort();
}
#define CHECK(value) do { ++checks; if(!(value))fail(#value,__LINE__); } while(0)

void beforeAllocate(std::size_t size) {
  if(rtTracking)++rtNew;
  if(size==gateSize.load(std::memory_order_relaxed)&&gateEnabled.load(std::memory_order_acquire)) {
    CHECK(!rtTracking);
    std::unique_lock<std::mutex> lock(gateMutex);
    if(gateEnabled.load(std::memory_order_relaxed)) {
      gateEntered=true;gateWake.notify_all();
      gateWake.wait(lock,[]{return gateReleased;});
    }
  }
}
void recordAllocation(void* pointer,std::size_t size) {
  // The harness identifies new/delete ownership for this final concrete type
  // by its allocation size. Other SDK/container allocations have different
  // sizes in these bounded scenarios; ASan supplements this lifecycle check.
  if(size!=sizeof(Engine))return;
  const auto index=engineAllocations.fetch_add(1);
  CHECK(index<allocations.size());
  allocations[index].pointer.store(pointer);
  allocations[index].live.store(true);
}
void* allocate(std::size_t size) {
  beforeAllocate(size);
  auto* result=std::malloc(size?size:1);
  if(!result)throw std::bad_alloc();
  recordAllocation(result,size);return result;
}
void* allocateAligned(std::size_t size,std::size_t alignment) {
  beforeAllocate(size);
#ifdef _WIN32
  void* result=_aligned_malloc(size?size:1,std::max(alignment,sizeof(void*)));
  if(!result)throw std::bad_alloc();
#else
  void* result=nullptr;
  if(posix_memalign(&result,std::max(alignment,sizeof(void*)),size?size:1))throw std::bad_alloc();
#endif
  recordAllocation(result,size);return result;
}
void release(void* pointer,bool aligned=false) noexcept {
  if(!pointer)return;
  if(rtTracking)++rtDelete;
  const auto count=engineAllocations.load();
  for(std::size_t n=0;n<count;++n) {
    if(allocations[n].pointer.load()==pointer&&allocations[n].live.exchange(false)) {
      ++engineFrees;if(rtTracking)++engineRtFrees;break;
    }
  }
#ifdef _WIN32
  if(aligned)_aligned_free(pointer);else std::free(pointer);
#else
  (void)aligned;std::free(pointer);
#endif
}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void operator delete(void* p)noexcept{release(p);}
void operator delete[](void* p)noexcept{release(p);}
void operator delete(void* p,std::size_t)noexcept{release(p);}
void operator delete[](void* p,std::size_t)noexcept{release(p);}
void* operator new(std::size_t n,std::align_val_t a){return allocateAligned(n,static_cast<std::size_t>(a));}
void* operator new[](std::size_t n,std::align_val_t a){return allocateAligned(n,static_cast<std::size_t>(a));}
void operator delete(void* p,std::align_val_t)noexcept{release(p,true);}
void operator delete[](void* p,std::align_val_t)noexcept{release(p,true);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{release(p,true);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{release(p,true);}

namespace {
struct RtProbe {
  RtProbe(){CHECK(!rtTracking);rtNew=rtDelete=0;rtTracking=true;}
  ~RtProbe(){rtTracking=false;CHECK(rtNew==0&&rtDelete==0);++probes;}
};
struct Gate {
  explicit Gate(std::size_t size=sizeof(Engine)){std::lock_guard<std::mutex> lock(gateMutex);CHECK(!gateEnabled);gateEntered=false;gateReleased=false;gateSize=size;gateEnabled=true;}
  void wait(){std::unique_lock<std::mutex> lock(gateMutex);CHECK(gateWake.wait_for(lock,5s,[]{return gateEntered;}));}
  void open(){std::lock_guard<std::mutex> lock(gateMutex);gateEnabled=false;gateReleased=true;gateWake.notify_all();}
  ~Gate(){open();}
};
bool live(const Engine* engine) {
  for(std::size_t n=0;n<engineAllocations.load();++n)
    if(allocations[n].pointer.load()==engine&&allocations[n].live.load())return true;
  return false;
}
std::size_t liveCount(){return engineAllocations.load()-engineFrees.load();}
struct Pcm {
  std::array<float,512> left{},right{};
};
float peak(const std::array<float,512>& samples) {
  float result=0;for(const auto sample:samples)result=std::max(result,std::abs(sample));return result;
}
void render(Engine& engine,Pcm& pcm) {
  engine.render(pcm.left.data(),pcm.right.data(),static_cast<int>(pcm.left.size()));
  CHECK(engine.healthy());
  for(std::size_t n=0;n<pcm.left.size();++n)
    CHECK(std::isfinite(pcm.left[n])&&std::isfinite(pcm.right[n])&&
      std::abs(pcm.left[n])<=1&&std::abs(pcm.right[n])<=1);
}
const char* comparisonContext="direct exchange";
void same(const Pcm& a,const Pcm& b){
  if(a.left!=b.left||a.right!=b.right) {
    for(std::size_t n=0;n<a.left.size();++n)if(a.left[n]!=b.left[n]||a.right[n]!=b.right[n]) {
      std::fprintf(stderr,"PCM mismatch (%s) frame%zu actual=%g/%g expected=%g/%g\n",
        comparisonContext,n,a.left[n],a.right[n],b.left[n],b.right[n]);break;
    }
  }
  CHECK(a.left==b.left&&a.right==b.right);
}
std::vector<std::uint8_t> fixture() {
  std::ifstream file(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
  CHECK(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
Tone tone(const std::vector<std::uint8_t>& bytes,std::size_t selected=0) {
  const auto bank=tetorica::vab::parse({bytes.data(),bytes.size()});
  return tetorica::vab::prepareTone(bank,selected);
}
Tone sideTone(const std::vector<std::uint8_t>& bytes,bool right) {
  auto result=tone(bytes);result.volumeLeft=right?0:1;result.volumeRight=right?1:0;return result;
}
std::unique_ptr<Engine> prepared(const Tone& source,double rate=44100) {
  auto result=std::make_unique<Engine>(source);CHECK(result->prepare(rate));return result;
}
bool begin(Exchange& exchange,double rate=48000) {
  RtProbe probe;return exchange.beginBlock(rate);
}
void matchContinuation(Exchange& exchange,Engine& reference) {
  Pcm actual,expected;RtProbe probe;
  for(int n=0;n<8;++n){render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
}
void matchNewNote(Exchange& exchange,const Tone& source,double rate) {
  Engine reference(source);CHECK(reference.prepare(rate));
  Pcm actual,expected;
  {
    RtProbe probe;
    CHECK(exchange.instrument().sampleRate()==rate);
    CHECK(exchange.instrument().activeVoiceCount()==0);
    exchange.instrument().panic();reference.panic(); // Align adapter phase after any silent blocks.
    exchange.instrument().noteOn(60,127);reference.noteOn(60,127);
    for(int n=0;n<8;++n){render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
  }
  CHECK(std::max(peak(actual.left),peak(actual.right))>.001f);
}
Loader::Status waitStatus(Loader& loader,Exchange::Generation generation,Loader::State state) {
  const auto deadline=Clock::now()+5s;
  for(;;) {
    auto status=loader.status();
    CHECK(status.generation==generation);
    if(status.state!=Loader::State::Loading){
      if(status.state!=state)std::fprintf(stderr,"Unexpected loader status (%s): got %d expected %d error=%s\n",comparisonContext,static_cast<int>(status.state),static_cast<int>(state),status.error.c_str());
      CHECK(status.state==state);return status;}
    CHECK(Clock::now()<deadline);
    // Bounded completion polling; correctness never depends on this interval.
    std::this_thread::sleep_for(1ms);
  }
}

struct FileFixtures {
  std::filesystem::path directory;
  FileFixtures(){
    directory=std::filesystem::temp_directory_path()/
        ("tetorica-spu-loader-"+std::to_string(Clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(directory));
  }
  ~FileFixtures(){std::error_code error;std::filesystem::remove_all(directory,error);}
  std::string path(const std::string& name)const{return (directory/std::filesystem::u8path(name)).u8string();}
  std::string write(const std::string& name,const std::vector<std::uint8_t>& bytes){
    const auto result=path(name);std::ofstream out(std::filesystem::u8path(result),std::ios::binary|std::ios::trunc);
    CHECK(out.good());out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    out.close();CHECK(!out.fail());return result;
  }
};

void baseline(const std::vector<std::uint8_t>&) {
  Exchange exchange;Engine reference;Pcm actual,expected;
  CHECK(exchange.latestGeneration()==0&&exchange.adoptedGeneration()==0);
  {
    RtProbe probe;
    CHECK(exchange.instrument().prepare(48000)&&reference.prepare(48000));
    exchange.instrument().noteOn(60,110);reference.noteOn(60,110);
    for(int n=0;n<32;++n){CHECK(!exchange.beginBlock(48000));render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
    exchange.instrument().noteOff(60);reference.noteOff(60);
    for(int n=0;n<8;++n){CHECK(!exchange.beginBlock(48000));render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
  }
}

void blockBoundaryAndCurrentRate(const std::vector<std::uint8_t>& bytes) {
  Exchange exchange;Engine original;Pcm actual,expected;
  CHECK(exchange.instrument().prepare(48000)&&original.prepare(48000));
  exchange.instrument().noteOn(60,127);original.noteOn(60,127);
  const auto* previous=&exchange.instrument();
  auto left=sideTone(bytes,false);
  auto candidate=prepared(left,8000);const auto* candidateAddress=candidate.get();
  const auto generation=exchange.beginRequest();CHECK(exchange.publish(generation,std::move(candidate)));
  CHECK(&exchange.instrument()==previous&&exchange.adoptedGeneration()==0);
  {RtProbe probe;render(exchange.instrument(),actual);render(original,expected);same(actual,expected);}
  CHECK(begin(exchange,96000));
  CHECK(&exchange.instrument()==candidateAddress&&exchange.adoptedGeneration()==generation);
  CHECK(exchange.instrument().sampleRate()==96000&&exchange.instrument().activeVoiceCount()==0);
  {RtProbe probe;render(exchange.instrument(),actual);CHECK(peak(actual.left)==0&&peak(actual.right)==0);}
  CHECK(live(previous));exchange.collectRetired();CHECK(!live(previous));
  matchNewNote(exchange,left,96000);
}

void staleAndSuperseded(const std::vector<std::uint8_t>& bytes) {
  Exchange exchange;
  const auto oldGeneration=exchange.beginRequest();auto old=prepared(sideTone(bytes,false));const auto* oldAddress=old.get();
  const auto latest=exchange.beginRequest();auto right=sideTone(bytes,true);
  CHECK(exchange.publish(latest,prepared(right)));
  const bool staleAccepted=exchange.publish(oldGeneration,std::move(old));
  CHECK(!staleAccepted);CHECK(!live(oldAddress));
  CHECK(begin(exchange));CHECK(exchange.adoptedGeneration()==latest);matchNewNote(exchange,right,48000);
  exchange.collectRetired();
  auto duplicate=prepared(sideTone(bytes,false));const auto* duplicateAddress=duplicate.get();
  const bool duplicateAccepted=exchange.publish(latest,std::move(duplicate));
  CHECK(!duplicateAccepted);CHECK(!live(duplicateAddress));
  CHECK(!begin(exchange));CHECK(exchange.adoptedGeneration()==latest);

  const auto pendingGeneration=exchange.beginRequest();auto pending=prepared(right);const auto* pendingAddress=pending.get();
  CHECK(exchange.publish(pendingGeneration,std::move(pending)));
  const auto next=exchange.beginRequest();CHECK(exchange.publish(next,prepared(sideTone(bytes,false))));
  CHECK(!live(pendingAddress)); // Superseded pending destruction is on this producer.
  CHECK(begin(exchange));matchNewNote(exchange,sideTone(bytes,false),48000);
}

void cancelledAndInvalidRate(const std::vector<std::uint8_t>& bytes) {
  Exchange exchange;Engine reference;Pcm actual,expected;
  CHECK(exchange.instrument().prepare(48000)&&reference.prepare(48000));
  exchange.instrument().noteOn(67,100);reference.noteOn(67,100);
  const auto* active=&exchange.instrument();
  auto stale=prepared(sideTone(bytes,true));const auto* staleAddress=stale.get();
  CHECK(exchange.publish(exchange.beginRequest(),std::move(stale)));
  exchange.beginRequest(); // A newer failing request has no candidate to publish.
  CHECK(!begin(exchange));CHECK(&exchange.instrument()==active&&exchange.adoptedGeneration()==0);
  CHECK(live(staleAddress));exchange.collectRetired();CHECK(!live(staleAddress));
  {RtProbe probe;render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
  const auto generation=exchange.beginRequest();auto invalidRate=prepared(sideTone(bytes,false));const auto* address=invalidRate.get();
  CHECK(exchange.publish(generation,std::move(invalidRate)));
  CHECK(!begin(exchange,std::numeric_limits<double>::quiet_NaN()));
  CHECK(&exchange.instrument()==active&&exchange.instrument().sampleRate()==48000);
  CHECK(live(address));exchange.collectRetired();CHECK(!live(address));
  {RtProbe probe;render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
  CHECK(!exchange.publish(0,prepared(sideTone(bytes,false))));
  CHECK(!exchange.publish(exchange.latestGeneration(),nullptr));
}

void fullRetirement(const std::vector<std::uint8_t>& bytes) {
  for(const bool cancel:{false,true}) {
    Exchange exchange;std::array<const Engine*,Exchange::kRetiredCapacity> retired{};
    for(std::size_t n=0;n<retired.size();++n) {
      retired[n]=&exchange.instrument();
      CHECK(exchange.publish(exchange.beginRequest(),prepared(sideTone(bytes,n%2!=0))));
      CHECK(begin(exchange));
    }
    const auto* active=&exchange.instrument();const auto adopted=exchange.adoptedGeneration();
    auto last=sideTone(bytes,false);auto candidate=prepared(last);const auto* pending=candidate.get();
    const auto generation=exchange.beginRequest();CHECK(exchange.publish(generation,std::move(candidate)));
    CHECK(!begin(exchange));CHECK(&exchange.instrument()==active&&exchange.adoptedGeneration()==adopted&&live(pending));
    if(cancel){exchange.beginRequest();CHECK(!begin(exchange));CHECK(live(pending));}
    exchange.collectRetired();for(const auto* engine:retired)CHECK(!live(engine));
    CHECK(begin(exchange)==!cancel);
    if(cancel) {
      CHECK(&exchange.instrument()==active&&exchange.adoptedGeneration()==adopted&&live(pending));
      exchange.collectRetired();CHECK(!live(pending));
      CHECK(exchange.publish(exchange.beginRequest(),prepared(last)));CHECK(begin(exchange));
    } else CHECK(&exchange.instrument()==pending&&exchange.adoptedGeneration()==generation);
    CHECK(exchange.instrument().activeVoiceCount()==0);matchNewNote(exchange,last,48000);
    exchange.collectRetired();
  }
}

void loaderSuccessFailureAndLatest(const std::vector<std::uint8_t>& bytes) {
  Exchange exchange;Loader loader(exchange);
  CHECK(loader.status().state==Loader::State::Idle);
  const auto* builtin=&exchange.instrument();
  const auto first=loader.request(bytes,0);CHECK(waitStatus(loader,first,Loader::State::Ready).error.empty());
  CHECK(&exchange.instrument()==builtin&&exchange.adoptedGeneration()==0);
  comparisonContext="worker initial import";
  CHECK(begin(exchange));Engine importedReference(tone(bytes));CHECK(importedReference.prepare(48000));
  exchange.instrument().noteOn(60,127);importedReference.noteOn(60,127);
  matchContinuation(exchange,importedReference);
  const auto* imported=&exchange.instrument();

  const auto pending=loader.request(bytes,1);waitStatus(loader,pending,Loader::State::Ready);
  auto malformed=bytes;malformed[0]=0;
  const auto failed=loader.request(malformed,0);CHECK(!waitStatus(loader,failed,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==imported&&exchange.adoptedGeneration()==first);
  comparisonContext="worker failed request keeps active";
  matchContinuation(exchange,importedReference);

  auto noise=bytes;noise[0x820+22]=255;noise[0x820+23]=0;
  const auto unsupported=loader.request(noise,0);CHECK(!waitStatus(loader,unsupported,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==imported);
  matchContinuation(exchange,importedReference);
  const auto invalidSelection=loader.request(bytes,999);
  CHECK(!waitStatus(loader,invalidSelection,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==imported);
  matchContinuation(exchange,importedReference);
  auto flag4=bytes;flag4[3616+16+1]&=static_cast<std::uint8_t>(~4);flag4[3616+16+17]|=4;
  const auto unsupportedLoop=loader.request(flag4,0);
  CHECK(!waitStatus(loader,unsupportedLoop,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==imported);
  matchContinuation(exchange,importedReference);
  const auto oversized=loader.request(std::vector<std::uint8_t>(tetorica::vab::kMaxFileBytes+1),0);
  CHECK(!waitStatus(loader,oversized,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==imported);
  matchContinuation(exchange,importedReference);

  for(int n=0;n<24;++n)loader.request(n%3?bytes:malformed,static_cast<std::size_t>(n%2));
  auto rightBytes=bytes;rightBytes[0x820+3]=127;
  const auto latest=loader.request(rightBytes,0);waitStatus(loader,latest,Loader::State::Ready);
  CHECK(begin(exchange));CHECK(exchange.adoptedGeneration()==latest);
  comparisonContext="worker rapid latest pan";
  matchNewNote(exchange,tone(rightBytes),48000);
  const auto builtinGeneration=loader.requestBuiltin();waitStatus(loader,builtinGeneration,Loader::State::Ready);
  CHECK(begin(exchange));CHECK(exchange.adoptedGeneration()==builtinGeneration);
  comparisonContext="worker builtin return";
  Engine reference;Pcm actual,expected;CHECK(reference.prepare(48000));
  {RtProbe probe;exchange.instrument().noteOn(60,127);reference.noteOn(60,127);render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
}

void inFlightSupersession(const std::vector<std::uint8_t>& bytes) {
  for(const bool failLatest:{false,true}) {
    Exchange exchange;Loader loader(exchange);Gate gate;
    const auto old=loader.request(bytes,0);gate.wait();
    auto nextBytes=bytes;if(failLatest)nextBytes[0]=0;else nextBytes[0x820+3]=127;
    const auto latest=loader.request(nextBytes,0);CHECK(latest>old);
    gate.open();
    const auto status=waitStatus(loader,latest,failLatest?Loader::State::Failed:Loader::State::Ready);
    CHECK(status.generation==latest);
    CHECK(begin(exchange)==!failLatest);
    CHECK(exchange.adoptedGeneration()==(failLatest?0:latest));
    if(!failLatest)matchNewNote(exchange,tone(nextBytes),48000);
    else {
      CHECK(!status.error.empty());Engine reference;Pcm actual,expected;
      CHECK(exchange.instrument().prepare(48000)&&reference.prepare(48000));
      {RtProbe probe;exchange.instrument().noteOn(60,127);reference.noteOn(60,127);render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
    }
  }
}

void workerFilesAndSelections(const std::vector<std::uint8_t>& bytes) {
  FileFixtures files;Exchange exchange;Loader loader(exchange);Engine reference;
  CHECK(exchange.instrument().prepare(48000)&&reference.prepare(48000));
  exchange.instrument().noteOn(60,127);reference.noteOn(60,127);
  auto truncated=bytes;truncated.pop_back();
  const auto empty=files.write("empty.vab",{});
  const auto shortFile=files.write("truncated.vab",truncated);
  const auto malformed=files.write("invalid.vab",std::vector<std::uint8_t>(128,42));
  const auto oversized=files.path("oversized.vab");
  {std::ofstream out(oversized,std::ios::binary);CHECK(out.good());out.seekp(tetorica::vab::kMaxFileBytes);out.put(0);CHECK(out.good());}
  for(const auto& path:std::vector<std::string>{"",files.path("missing.vab"),files.directory.u8string(),empty,shortFile,malformed,oversized}){
    const auto gen=loader.requestFile(path);
    CHECK(!waitStatus(loader,gen,Loader::State::Failed).error.empty());CHECK(!begin(exchange));
    matchContinuation(exchange,reference);
  }
  auto nulPath=files.path("valid.vab");nulPath.push_back('\0');nulPath+="ignored";
  auto gen=loader.requestFile(nulPath);CHECK(!waitStatus(loader,gen,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange));matchContinuation(exchange,reference);

  const auto unreadable=files.write("unreadable.vab",bytes);
  std::error_code permissionError;
  std::filesystem::permissions(unreadable,std::filesystem::perms::none,std::filesystem::perm_options::replace,permissionError);
  if(!permissionError){
    std::ifstream permissionProbe(unreadable,std::ios::binary);
    if(!permissionProbe){gen=loader.requestFile(unreadable);CHECK(!waitStatus(loader,gen,Loader::State::Failed).error.empty());CHECK(!begin(exchange));matchContinuation(exchange,reference);}
    permissionProbe.close();
    std::filesystem::permissions(unreadable,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace,permissionError);
  }

  const auto valid=files.write("音色.vab",bytes);
  gen=loader.requestFile(valid);waitStatus(loader,gen,Loader::State::Ready);CHECK(begin(exchange));
  CHECK(exchange.snapshotDefinition()->program()==3&&exchange.snapshotDefinition()->tone()==0);
  matchNewNote(exchange,tone(bytes),48000);
  const auto source=exchange.snapshotDefinition();
  CHECK(std::filesystem::remove(std::filesystem::u8path(valid)));
  gen=loader.requestSelection(source,3,1);waitStatus(loader,gen,Loader::State::Ready);CHECK(begin(exchange));
  CHECK(exchange.snapshotDefinition()->program()==3&&exchange.snapshotDefinition()->tone()==1);
  matchNewNote(exchange,tone(bytes,1),48000); //Embedded bank survives deletion of source file.
  const auto* current=&exchange.instrument();
  const auto currentDef=exchange.snapshotDefinition();
  gen=loader.requestSelection(source,11,1);CHECK(!waitStatus(loader,gen,Loader::State::Failed).error.empty());
  CHECK(!begin(exchange)&&&exchange.instrument()==current&&exchange.snapshotDefinition()==currentDef);
  gen=loader.requestSelection(nullptr,3,0);waitStatus(loader,gen,Loader::State::Failed);CHECK(!begin(exchange));
  gen=loader.requestSelection(tetorica::InstrumentDefinition::builtin(),0,0);
  waitStatus(loader,gen,Loader::State::Failed);CHECK(!begin(exchange));

  auto firstNoise=bytes;firstNoise[0x820+22]=255;firstNoise[0x820+23]=0;
  const auto noisePath=files.write("first-noise.vab",firstNoise);
  gen=loader.requestFile(noisePath);waitStatus(loader,gen,Loader::State::Ready);CHECK(begin(exchange));
  CHECK(exchange.snapshotDefinition()->program()==3&&exchange.snapshotDefinition()->tone()==1);
  matchNewNote(exchange,tone(firstNoise,1),48000);
  const auto playable=exchange.snapshotDefinition();
  gen=loader.requestFile(noisePath,0);waitStatus(loader,gen,Loader::State::Failed);
  CHECK(!begin(exchange)&&exchange.snapshotDefinition()==playable); //Explicit index must not skip.
  auto none=firstNoise;none[0x840+22]=255;none[0x840+23]=0;none[0xa20+22]=0;none[0xa20+23]=0;
  gen=loader.requestFile(files.write("none-playable.vab",none));
  CHECK(waitStatus(loader,gen,Loader::State::Failed).error.find("No playable Tone")!=std::string::npos);
  CHECK(!begin(exchange)&&exchange.snapshotDefinition()==playable);
  auto flag4=bytes;flag4[3616+16+1]=4;
  gen=loader.requestFile(files.write("unsupported-loop-first.vab",flag4));
  waitStatus(loader,gen,Loader::State::Ready);CHECK(begin(exchange));
  CHECK(exchange.snapshotDefinition()->program()==11&&exchange.snapshotDefinition()->tone()==0);
  Engine oneShot(tone(flag4,2));CHECK(oneShot.prepare(48000));Pcm actual,expected;float firstPeak=0;
  {RtProbe probe;exchange.instrument().noteOn(69,127);oneShot.noteOn(69,127);
   for(int i=0;i<8;++i){render(exchange.instrument(),actual);render(oneShot,expected);same(actual,expected);firstPeak=std::max(firstPeak,peak(actual.left));}}
  CHECK(firstPeak>0&&peak(actual.left)==0);

  auto programBytes=bytes;programBytes[0x820+22]=255;programBytes[0x840+22]=2;
  auto programSource=tetorica::InstrumentDefinition::fromVab(programBytes,11,0);
  gen=loader.requestProgram(programSource,3);waitStatus(loader,gen,Loader::State::Ready);CHECK(begin(exchange));
  CHECK(exchange.snapshotDefinition()->program()==3&&exchange.snapshotDefinition()->tone()==1);
  auto selectedProgram=exchange.snapshotDefinition();
  // Both P3 Tones unavailable, although P11 remains playable: never silently
  // switch to another Program when the user explicitly chose Program3.
  programBytes[0x840+22]=255;
  programSource=tetorica::InstrumentDefinition::fromVab(programBytes,11,0);
  gen=loader.requestProgram(programSource,3);
  CHECK(waitStatus(loader,gen,Loader::State::Failed).error.find("Program")!=std::string::npos);
  CHECK(!begin(exchange)&&exchange.snapshotDefinition()==selectedProgram);
  gen=loader.requestProgram(programSource,0);waitStatus(loader,gen,Loader::State::Failed);CHECK(!begin(exchange));
  gen=loader.requestProgram(nullptr,3);waitStatus(loader,gen,Loader::State::Failed);CHECK(!begin(exchange));
}

void changedFileAndRestoreSupersession(const std::vector<std::uint8_t>& bytes) {
  FileFixtures files;
  for(const bool grow:{false,true}){
    comparisonContext=grow?"file grew after measurement":"file shrank after measurement";
    Exchange exchange;Loader loader(exchange);const auto file=files.write(grow?"grow.vab":"shrink.vab",bytes);
    Gate gate(bytes.size()); //Worker has measured size; pauses before allocating its file buffer.
    const auto gen=loader.requestFile(file,0);gate.wait();
    if(grow){std::ofstream out(file,std::ios::binary|std::ios::app);out.put(0);out.close();CHECK(!out.fail());}
    else std::filesystem::resize_file(file,16);
    gate.open();CHECK(!waitStatus(loader,gen,Loader::State::Failed).error.empty());
    CHECK(!begin(exchange)&&exchange.adoptedGeneration()==0);
  }
  const auto file=files.write("restore-supersession.vab",bytes);
  {
    Exchange next;Loader loader(next);Gate gate;
    const auto old=loader.requestFile(file,0);gate.wait();
    auto right=bytes;right[0x820+3]=127;
    const auto latest=loader.requestFile(files.write("latest.vab",right),0);
    CHECK(latest>old);gate.open();waitStatus(loader,latest,Loader::State::Ready);
    CHECK(begin(next)&&next.adoptedGeneration()==latest);matchNewNote(next,tone(right),48000);
  }
  Exchange exchange;
  auto definition=tetorica::InstrumentDefinition::fromVab(bytes,3,1);
  auto hostPrepared=tetorica::prepareDefinition(*definition);
  auto loader=std::make_unique<Loader>(exchange);Gate gate;
  const auto old=loader->requestFile(file,0);gate.wait();
  const auto restored=exchange.commitPrepared(std::move(hostPrepared),definition);
  CHECK(restored>old&&begin(exchange));
  const auto status=loader->status();
  CHECK(status.generation==old&&status.state==Loader::State::Failed&&!status.error.empty());
  CHECK(exchange.snapshotDefinition()==definition);
  gate.open();loader.reset(); //Join old completion; it must not publish over host restore.
  CHECK(exchange.latestGeneration()==restored&&exchange.adoptedGeneration()==restored);
  CHECK(exchange.snapshotDefinition()==definition);matchNewNote(exchange,tone(bytes,1),48000);
}

void workerPcmCycles(const std::vector<std::uint8_t>& bytes) {
  using Shape=tetorica::WaveformShape;
  using Definition=tetorica::InstrumentDefinition;
  Exchange exchange;Loader loader(exchange);Pcm actual,expected;
  for(const auto shape:{Shape::Sine,Shape::Triangle,Shape::Saw,Shape::Square}) {
    const auto cycle=tetorica::makePcmCycle(shape);
    Engine reference(cycle);CHECK(reference.prepare(48000));
    const auto generation=loader.requestPcmCycle(cycle);
    waitStatus(loader,generation,Loader::State::Ready);
    const auto snapshot=exchange.snapshotDefinition();
    CHECK(snapshot&&snapshot->kind()==Definition::Kind::PcmCycle&&snapshot->pcmCycle()==cycle);
    // Ready is saveable even before the next audio callback. A successful
    // replacement silences the old keys; fresh notes use the authored cycle.
    const auto saved=tetorica::encodeState(snapshot,42.5);
    CHECK(tetorica::decodeState({saved.data(),saved.size()}).definition->pcmCycle()==cycle);
    CHECK(begin(exchange)&&exchange.adoptedGeneration()==generation);
    {
      RtProbe probe;
      CHECK(exchange.instrument().activeVoiceCount()==0);
      render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);
      CHECK(peak(actual.left)==0&&peak(actual.right)==0);
      exchange.instrument().noteOn(60,100,15);reference.noteOn(60,100,15);
      for(int block=0;block<8;++block){render(exchange.instrument(),actual);render(reference,expected);same(actual,expected);}
    }
    CHECK(peak(actual.left)>.001f);
    exchange.collectRetired();
  }

  // Caller edits after submission cannot mutate the job's owned PCM.
  auto custom=tetorica::makePcmCycle(Shape::Triangle);custom[17]=-.1234567f;
  const auto original=custom;
  const auto pending=loader.requestPcmCycle(custom);custom.fill(1.f);
  waitStatus(loader,pending,Loader::State::Ready);
  const auto ready=exchange.snapshotDefinition();CHECK(ready->pcmCycle()==original);
  const auto previousGeneration=exchange.latestGeneration();
  for(float invalid:{-1.01f,1.01f,std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()}) {
    auto bad=original;bad[43]=invalid;bool rejected=false;
    try {loader.requestPcmCycle(bad);}catch(const std::exception&){rejected=true;}
    CHECK(rejected&&exchange.latestGeneration()==previousGeneration);
    CHECK(exchange.snapshotDefinition()==ready&&loader.status().state==Loader::State::Ready);
  }
  CHECK(begin(exchange)&&exchange.adoptedGeneration()==pending);
  Engine reference(original);CHECK(reference.prepare(48000));
  {RtProbe probe;exchange.instrument().noteOn(64,127);reference.noteOn(64,127);}
  matchContinuation(exchange,reference);

  // VAB-only operations remain rejected rather than interpreting PCM bytes as
  // an archive. A failed request leaves the current waveform audible.
  auto failed=loader.requestProgram(ready,3);waitStatus(loader,failed,Loader::State::Failed);
  CHECK(!begin(exchange)&&exchange.snapshotDefinition()==ready);matchContinuation(exchange,reference);
  failed=loader.requestSelection(ready,3,0);waitStatus(loader,failed,Loader::State::Failed);
  CHECK(!begin(exchange)&&exchange.snapshotDefinition()==ready);matchContinuation(exchange,reference);

  const auto imported=loader.request(bytes,0);waitStatus(loader,imported,Loader::State::Ready);
  CHECK(begin(exchange)&&exchange.snapshotDefinition()->kind()==Definition::Kind::Vab);
  const auto builtin=loader.requestBuiltin();waitStatus(loader,builtin,Loader::State::Ready);
  CHECK(begin(exchange)&&exchange.snapshotDefinition()->kind()==Definition::Kind::Builtin);
}

void pcmCycleSupersession(const std::vector<std::uint8_t>&) {
  using Shape=tetorica::WaveformShape;
  const auto first=tetorica::makePcmCycle(Shape::Triangle);
  const auto second=tetorica::makePcmCycle(Shape::Square);
  {
    Exchange exchange;Loader loader(exchange);Gate gate;
    const auto old=loader.requestPcmCycle(first);gate.wait();
    const auto latest=loader.requestPcmCycle(second);CHECK(latest>old);
    gate.open();waitStatus(loader,latest,Loader::State::Ready);
    CHECK(begin(exchange)&&exchange.adoptedGeneration()==latest);
    CHECK(exchange.snapshotDefinition()->pcmCycle()==second);
  }
  {
    Exchange exchange;
    const auto restoredDefinition=tetorica::InstrumentDefinition::fromPcmCycle(second);
    auto restoredEngine=tetorica::prepareDefinition(*restoredDefinition);
    auto loader=std::make_unique<Loader>(exchange);Gate gate;
    const auto old=loader->requestPcmCycle(first);gate.wait();
    const auto restored=exchange.commitPrepared(std::move(restoredEngine),restoredDefinition);
    CHECK(restored>old&&begin(exchange));
    gate.open();loader.reset(); // Join the stale worker before final assertions.
    CHECK(exchange.latestGeneration()==restored&&exchange.adoptedGeneration()==restored);
    CHECK(exchange.snapshotDefinition()==restoredDefinition);
    Engine reference(second);CHECK(reference.prepare(48000));
    {RtProbe probe;exchange.instrument().noteOn(60,127);reference.noteOn(60,127);}
    matchContinuation(exchange,reference);
  }
}

void workerMorphEndpoints(const std::vector<std::uint8_t>& bytes) {
  using Definition=tetorica::InstrumentDefinition;
  using Shape=tetorica::WaveformShape;
  const auto equal=[](const tetorica::MorphEndpoints& a,const tetorica::MorphEndpoints& b){return a.a==b.a&&a.b==b.b;};
  tetorica::MorphEndpoints endpoints{tetorica::makePcmCycle(Shape::Square),tetorica::makePcmCycle(Shape::Triangle)};
  endpoints.a[17]=-.1234567f;endpoints.b[111]=.3456789f;
  const auto original=endpoints;
  Exchange exchange;Loader loader(exchange);FileFixtures files;
  auto source=Definition::fromVab(bytes,3,1);
  exchange.commitPrepared(tetorica::prepareDefinition(*source),source);CHECK(begin(exchange));
  const auto applied=loader.requestMorphEndpoints(source,endpoints);endpoints.a.fill(0);endpoints.b.fill(0);
  waitStatus(loader,applied,Loader::State::Ready);
  const auto ready=exchange.snapshotDefinition();
  CHECK(ready->kind()==Definition::Kind::Vab&&ready->bankBytes()==bytes&&ready->program()==3&&ready->tone()==1);
  CHECK(equal(ready->morphEndpoints(),original));
  const auto saved=tetorica::encodeState(ready,37,{}, {},{}, {},{true,.375});
  CHECK(equal(tetorica::decodeState({saved.data(),saved.size()}).definition->morphEndpoints(),original));
  const auto generation=exchange.latestGeneration();
  for(float invalid:{-1.01f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
    auto bad=original;bad.b[111]=invalid;bool rejected=false;
    try{loader.requestMorphEndpoints(ready,bad);}catch(const std::exception&){rejected=true;}
    CHECK(rejected&&exchange.latestGeneration()==generation&&exchange.snapshotDefinition()==ready);
  }
  bool rejected=false;try{loader.requestMorphEndpoints(nullptr,original);}catch(const std::exception&){rejected=true;}
  CHECK(rejected&&exchange.latestGeneration()==generation&&exchange.snapshotDefinition()==ready);
  // A second request must capture the current pending definition BEFORE its
  // generation invalidates it; falling back to the active source loses A/B.
  auto gen=loader.requestBuiltin();waitStatus(loader,gen,Loader::State::Ready);
  CHECK(exchange.snapshotDefinition()->kind()==Definition::Kind::Builtin&&equal(exchange.snapshotDefinition()->morphEndpoints(),original));
  CHECK(begin(exchange)&&equal(exchange.instrument().morphEndpoints(),original));
  const auto inspect=[&](Exchange::Generation value,Definition::Kind kind) {
    waitStatus(loader,value,Loader::State::Ready);const auto snapshot=exchange.snapshotDefinition();
    CHECK(snapshot->kind()==kind&&equal(snapshot->morphEndpoints(),original));
    CHECK(begin(exchange)&&equal(exchange.instrument().morphEndpoints(),original));exchange.collectRetired();
  };
  inspect(loader.request(bytes,0),Definition::Kind::Vab);
  inspect(loader.requestSelection(exchange.snapshotDefinition(),3,1),Definition::Kind::Vab);
  inspect(loader.requestProgram(exchange.snapshotDefinition(),11),Definition::Kind::Vab);
  inspect(loader.requestPcmCycle(tetorica::makePcmCycle(Shape::Triangle)),Definition::Kind::PcmCycle);
  inspect(loader.requestFile(files.write("morph preserved.vab",bytes)),Definition::Kind::Vab);
  inspect(loader.requestFile(files.write("morph exact.vab",bytes),1),Definition::Kind::Vab);
  inspect(loader.requestBuiltin(),Definition::Kind::Builtin);
  {
    Gate gate;const auto old=loader.requestMorphEndpoints(exchange.snapshotDefinition(),original);gate.wait();
    auto newer=original;newer.b[9]=-.456789f;
    const auto latest=loader.requestMorphEndpoints(exchange.snapshotDefinition(),newer);CHECK(latest>old);
    gate.open();waitStatus(loader,latest,Loader::State::Ready);
    CHECK(begin(exchange)&&equal(exchange.snapshotDefinition()->morphEndpoints(),newer));
  }
  {
    auto restoredEndpoints=original;restoredEndpoints.a[7]=-.7654321f;
    auto restored=Definition::withMorphEndpoints(source,restoredEndpoints);
    auto prepared=tetorica::prepareDefinition(*restored);
    Gate gate;const auto old=loader.requestMorphEndpoints(exchange.snapshotDefinition(),original);gate.wait();
    const auto latest=exchange.commitPrepared(std::move(prepared),restored);CHECK(latest>old&&begin(exchange));
    gate.open();waitStatus(loader,old,Loader::State::Failed);
    CHECK(exchange.snapshotDefinition()==restored&&equal(exchange.instrument().morphEndpoints(),restoredEndpoints));
  }
}

void concurrentPublication(const std::vector<std::uint8_t>& bytes) {
  Exchange exchange;const auto left=sideTone(bytes,false),right=sideTone(bytes,true);
  std::mutex mutex;std::condition_variable wake;int requested=0,completed=0;
  constexpr int phases=48;
  std::thread producer([&]{
    for(int phase=1;phase<=phases;++phase) {
      {std::unique_lock<std::mutex> lock(mutex);CHECK(wake.wait_for(lock,5s,[&]{return requested>=phase;}));}
      auto candidate=prepared(phase%2?left:right,8000);
      const auto generation=exchange.beginRequest();CHECK(exchange.publish(generation,std::move(candidate)));
      exchange.collectRetired();
      {std::lock_guard<std::mutex> lock(mutex);completed=phase;}wake.notify_all();
    }
  });
  Exchange::Generation previous=0;Pcm out;
  for(int phase=1;phase<=phases;++phase) {
    {std::lock_guard<std::mutex> lock(mutex);requested=phase;}wake.notify_all();
    {
      RtProbe probe;
      for(int block=0;block<64;++block) {
        const double rate=block%2?96000:48000;
        exchange.beginBlock(rate);
        auto& engine=exchange.instrument();
        if(engine.sampleRate()!=rate)CHECK(engine.prepare(rate));
        CHECK(exchange.adoptedGeneration()>=previous);previous=exchange.adoptedGeneration();
        engine.noteOn(60+(block%12),100,block%16);render(engine,out);engine.allNotesOff();
      }
    }
    {std::unique_lock<std::mutex> lock(mutex);CHECK(wake.wait_for(lock,5s,[&]{return completed>=phase;}));}
  }
  producer.join();exchange.collectRetired();begin(exchange,96000);
  CHECK(exchange.adoptedGeneration()==exchange.latestGeneration());
  // Adopt a fresh final engine before using a fresh PCM reference. Re-triggering
  // an already-played SPU is not a promise to reset its hardware filter history.
  CHECK(exchange.publish(exchange.beginRequest(),prepared(right)));CHECK(begin(exchange,96000));
  matchNewNote(exchange,right,96000);
}

void shutdownOwnership(const std::vector<std::uint8_t>& bytes) {
  // Exchange destruction must reclaim active, retired and unadopted pending.
  {
    Exchange exchange;
    for(int n=0;n<3;++n){CHECK(exchange.publish(exchange.beginRequest(),prepared(sideTone(bytes,n%2))));CHECK(begin(exchange));}
    CHECK(exchange.publish(exchange.beginRequest(),prepared(sideTone(bytes,true))));
  }
  // Stop while a worker has passed parsing and entered actual construction.
  // The allocator barrier supplies the ordering; no scheduler-delay assertion.
  {
    auto exchange=std::make_unique<Exchange>();
    auto loader=std::make_unique<Loader>(*exchange);Gate gate;
    loader->request(bytes,0);gate.wait();
    std::promise<void> entered,finished;
    auto enteredFuture=entered.get_future(),finishedFuture=finished.get_future();
    std::thread shutdown([owned=std::move(loader),&entered,&finished]()mutable{
      entered.set_value();owned.reset();finished.set_value();
    });
    CHECK(enteredFuture.wait_for(5s)==std::future_status::ready);
    gate.open();
    CHECK(finishedFuture.wait_for(5s)==std::future_status::ready);shutdown.join();
    exchange.reset();
  }
}

template<class Function>void run(const char* name,Function function,const std::vector<std::uint8_t>& bytes) {
  comparisonContext=name;
  const auto before=liveCount();function(bytes);CHECK(liveCount()==before);CHECK(engineRtFrees==0);
  std::printf("PASS %s\n",name);
}
}
int main() {
  const auto bytes=fixture();
  run("unchanged builtin PCM",baseline,bytes);
  run("block-boundary swap/current host rate",blockBoundaryAndCurrentRate,bytes);
  run("stale/superseded/duplicate publication",staleAndSuperseded,bytes);
  run("cancelled pending/invalid rate keeps active PCM",cancelledAndInvalidRate,bytes);
  run("full retirement defers then resumes",fullRetirement,bytes);
  run("worker success/failure/latest/builtin",loaderSuccessFailureAndLatest,bytes);
  run("deterministic in-flight supersession",inFlightSupersession,bytes);
  run("worker bounded files/first-playable/embedded selection",workerFilesAndSelections,bytes);
  run("changed file/host restore supersession",changedFileAndRestoreSupersession,bytes);
  run("PCM worker shapes/owned input/invalid pending/selection",workerPcmCycles,bytes);
  run("PCM latest request/host restore supersession",pcmCycleSupersession,bytes);
  run("Morph immutable endpoints/retained sources/latest/restore",workerMorphEndpoints,bytes);
  run("concurrent producer/audio publication",concurrentPublication,bytes);
  run("pending/retired/in-flight shutdown ownership",shutdownOwnership,bytes);
  CHECK(liveCount()==0&&engineAllocations==engineFrees&&engineRtFrees==0);
  std::printf("PASS engine exchange: %zu checks, %zu RT C++ new/delete probes; %zu instrument-size allocations/%zu frees, zero RT frees\n",
    checks.load(),probes.load(),engineAllocations.load(),engineFrees.load());
}
