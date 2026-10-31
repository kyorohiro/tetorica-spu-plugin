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
bool gateEntered = false, gateReleased = false;

[[noreturn]] void fail(const char* expression,int line) {
  std::fprintf(stderr,"FAIL line %d: %s\n",line,expression);std::abort();
}
#define CHECK(value) do { ++checks; if(!(value))fail(#value,__LINE__); } while(0)

void beforeAllocate(std::size_t size) {
  if(rtTracking)++rtNew;
  if(size==sizeof(Engine)&&gateEnabled.load(std::memory_order_acquire)) {
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
  Gate(){std::lock_guard<std::mutex> lock(gateMutex);CHECK(!gateEnabled);gateEntered=false;gateReleased=false;gateEnabled=true;}
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
    if(status.state!=Loader::State::Loading){CHECK(status.state==state);return status;}
    CHECK(Clock::now()<deadline);
    // Bounded completion polling; correctness never depends on this interval.
    std::this_thread::sleep_for(1ms);
  }
}

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
  run("concurrent producer/audio publication",concurrentPublication,bytes);
  run("pending/retired/in-flight shutdown ownership",shutdownOwnership,bytes);
  CHECK(liveCount()==0&&engineAllocations==engineFrees&&engineRtFrees==0);
  std::printf("PASS engine exchange: %zu checks, %zu RT C++ new/delete probes; %zu instrument-size allocations/%zu frees, zero RT frees\n",
    checks.load(),probes.load(),engineAllocations.load(),engineFrees.load());
}
