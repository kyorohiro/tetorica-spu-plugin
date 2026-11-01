// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "SoftwarePcm.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

namespace {
thread_local bool tracking = false;
thread_local unsigned allocations = 0, releases = 0;
void* allocate(std::size_t size) {
  if (tracking) ++allocations;
  if (auto* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void release(void* value) noexcept {
  if (tracking && value) ++releases;
  std::free(value);
}
void* allocateAligned(std::size_t size, std::size_t alignment) {
  if (tracking) ++allocations;
#ifdef _WIN32
  if (auto* value = _aligned_malloc(size ? size : 1, alignment)) return value;
#else
  void* value = nullptr;
  if (posix_memalign(&value, alignment, size ? size : 1) == 0) return value;
#endif
  throw std::bad_alloc();
}
void releaseAligned(void* value) noexcept {
  if (tracking && value) ++releases;
#ifdef _WIN32
  _aligned_free(value);
#else
  std::free(value);
#endif
}
}
void* operator new(std::size_t size) { return allocate(size); }
void* operator new[](std::size_t size) { return allocate(size); }
void operator delete(void* value) noexcept { release(value); }
void operator delete[](void* value) noexcept { release(value); }
void operator delete(void* value, std::size_t) noexcept { release(value); }
void operator delete[](void* value, std::size_t) noexcept { release(value); }
void* operator new(std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return allocateAligned(size, static_cast<std::size_t>(alignment)); }
void operator delete(void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete(void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }
void operator delete[](void* value, std::size_t, std::align_val_t) noexcept { releaseAligned(value); }

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)
using namespace tetorica;
namespace {
enum class Source { Sample, Vab, Noise, Pmon, Morph, Software, SoftwarePmon };
constexpr std::array<Source, 7> sources{Source::Sample, Source::Vab, Source::Noise,
  Source::Pmon, Source::Morph, Source::Software, Source::SoftwarePmon};
constexpr std::array<double, 3> rates{44100., 48000., 96000.};
constexpr EnvelopeRegisters heldRelease{0x000f, 0x1fdf};

struct Stereo {
  std::vector<float> left, right;
  explicit Stereo(int frames) : left(frames), right(frames) {}
};
void render(SpuInstrument& synth, Stereo& pcm, int quantum = 257) {
  for (int at = 0; at < static_cast<int>(pcm.left.size());) {
    const int count = std::min(quantum, static_cast<int>(pcm.left.size()) - at);
    synth.render(pcm.left.data() + at, pcm.right.data() + at, count);
    at += count;
  }
  CHECK(synth.healthy());
  for (const auto* channel : {&pcm.left, &pcm.right}) for (float value : *channel)
    CHECK(std::isfinite(value) && std::abs(value) <= 1);
}
bool same(const Stereo& actual, const Stereo& reference, int start = 0) {
  return std::equal(actual.left.begin() + start, actual.left.end(), reference.left.begin() + start) &&
         std::equal(actual.right.begin() + start, actual.right.end(), reference.right.begin() + start);
}
float peak(const Stereo& pcm, int start = 0) {
  float result = 0;
  for (const auto* channel : {&pcm.left, &pcm.right})
    for (int i = start; i < static_cast<int>(channel->size()); ++i)
      result = std::max(result, std::abs((*channel)[i]));
  return result;
}
// The causal resampler retains a previous/current pair of the mixed native
// output. A channel stop must not reset that shared history. At these rates,
// eight host samples exceed its two-native-sample history; Native44100 has none.
int historyFrames(double rate) { return rate == 44100. ? 0 : 8; }
int capacity(Source source) { return source == Source::Pmon || source == Source::SoftwarePmon ? 12 : 24; }

vab::PreparedTone bankTone() {
  const auto cycle = encodePcmCycle(makePcmCycle(WaveformShape::Sine));
  vab::PreparedTone tone;
  tone.adpcm.assign(cycle.begin(), cycle.end());
  tone.rootFrequencyHz = 44100. / 112;
  tone.decodedFrames = 112; tone.loopStartSamples = 0;
  tone.adsr1 = heldRelease.adsr1; tone.adsr2 = heldRelease.adsr2;
  tone.volumeLeft = .25; tone.volumeRight = .75; tone.reverb = true;
  return tone;
}
SoftwarePcmSample softwareSample() {
  std::vector<std::int16_t> pcm(1009);
  for (std::size_t i = 0; i < pcm.size(); ++i)
    pcm[i] = static_cast<std::int16_t>(std::lround(12000 * std::sin(6.2831853071795864769 * i / 112)));
  SoftwarePcmSample sample;
  sample.pcm = SoftwarePcmBuffer::copyOf(pcm);
  sample.baseFrequencyHz = 44100. / 112; sample.loop = true;
  sample.volumeLeft = -.75; sample.volumeRight = .25;
  sample.envelope = heldRelease; sample.reverb = true;
  return sample;
}
std::unique_ptr<SpuInstrument> instrument(Source source, double rate) {
  std::unique_ptr<SpuInstrument> result;
  if (source == Source::Vab) result = std::make_unique<SpuInstrument>(bankTone());
  else if (source == Source::Software || source == Source::SoftwarePmon)
    result = std::make_unique<SpuInstrument>(softwareSample());
  else result = std::make_unique<SpuInstrument>();
  CHECK(result->prepare(rate));
  result->setEnvelope(heldRelease);
  if (source == Source::Noise) CHECK(result->setNoise({true, 48}));
  if (source == Source::Pmon || source == Source::SoftwarePmon) {
    PmonSettings pmon; pmon.enabled = true; pmon.waveform = WaveformShape::Triangle;
    pmon.ratio = 1.5; pmon.depth = .4; pmon.decayMs = 0;
    CHECK(result->setPmon(pmon));
  }
  if (source == Source::Morph) {
    MorphEndpoints endpoints; endpoints.a = makePcmCycle(WaveformShape::Triangle);
    endpoints.b = makePcmCycle(WaveformShape::Square);
    CHECK(result->setMorphEndpoints(endpoints) && result->setMorph({true, .375}));
  }
  return result;
}

void stopsHeldDeferredAndReleased() {
  for (double rate : rates) for (auto source : sources) for (int state = 0; state < 3; ++state) {
    auto synth = instrument(source, rate);
    Stereo before(1031), after(2053);
    synth->noteOn(69, 127, 3);
    render(*synth, before); CHECK(peak(before) > .0001f);
    if (state == 1) synth->setSustain(true, 3);
    if (state != 0) synth->noteOff(69, 3);
    render(*synth, before); CHECK(peak(before) > .0001f);
    CHECK(synth->activeVoiceCount() == (state == 0 ? 1 : 0));
    const auto frame = synth->nativeFramesRendered();
    synth->allSoundOff(3);
    CHECK(synth->activeVoiceCount() == 0 && synth->nativeFramesRendered() == frame);
    render(*synth, after, 1);
    CHECK(peak(after, historyFrames(rate)) == 0);
    // Delayed key/pedal events cannot resurrect a cleared voice or its PMON pair.
    synth->noteOff(69, 3); synth->setSustain(false, 3);
    render(*synth, after); CHECK(peak(after) == 0);
  }
}

void unaffectedChannelMatchesIndependentVoice() {
  for (double rate : rates) for (auto source : sources) for (int state = 0; state < 3; ++state) {
    auto synth = instrument(source, rate), survivor = instrument(source, rate);
    Stereo actual(4099), reference(4099);
    // The reference has never received the target note or the operation under
    // test. Its live channel must continue with the exact same stereo samples.
    synth->noteOn(69, 100, 3);
    for (auto* current : {synth.get(), survivor.get()}) {
      current->setPitchBend(16383, 9); current->setSustain(true, 9);
      current->noteOn(60, 91, 9);
    }
    render(*synth, actual); render(*survivor, reference);
    if (state == 1) synth->setSustain(true, 3);
    if (state != 0) synth->noteOff(69, 3);
    render(*synth, actual); render(*survivor, reference);
    synth->allSoundOff(3); CHECK(synth->activeVoiceCount() == 1);
    render(*synth, actual, 1); render(*survivor, reference, 127);
    CHECK(same(actual, reference, historyFrames(rate)) && peak(reference) > .0001f);
    // The other channel's pedal and bend remain intact, including release tails.
    synth->noteOff(60, 9); survivor->noteOff(60, 9);
    render(*synth, actual); render(*survivor, reference); CHECK(same(actual, reference));
    synth->setSustain(false, 9); survivor->setSustain(false, 9);
    render(*synth, actual); render(*survivor, reference); CHECK(same(actual, reference));
  }
}

void retainedControllersAndFreshReuse() {
  for (double rate : rates) for (auto source : sources) {
    auto synth = instrument(source, rate), fresh = instrument(source, rate);
    Stereo actual(4099), reference(4099);
    synth->setPitchBend(16383, 3); synth->setSustain(true, 3);
    fresh->setSustain(true, 9);
    // Fill the complete pool so the next allocation reuses a killed physical
    // voice (including both sides of a PMON pair), rather than an unused slot.
    for (int i = 0; i < capacity(source); ++i) synth->noteOn(36 + i, 100, 3);
    render(*synth, actual); render(*fresh, reference); CHECK(peak(actual) > .001f);
    synth->allSoundOff(3);
    // Do not insert a settling render: an old audition fade/restart must never
    // leak into the new note. +2st bend survives CC120 on the same channel.
    synth->noteOn(67, 127, 3); fresh->noteOn(69, 127, 9);
    render(*synth, actual, 7); render(*fresh, reference, 4099);
    CHECK(same(actual, reference, historyFrames(rate)) && peak(reference) > .0001f);
    synth->noteOff(67, 3); fresh->noteOff(69, 9);
    CHECK(synth->activeVoiceCount() == 0);
    render(*synth, actual); render(*fresh, reference);
    CHECK(same(actual, reference) && peak(actual) > .0001f); // Same pedal still down.
    synth->setSustain(false, 3); fresh->setSustain(false, 9);
    render(*synth, actual); render(*fresh, reference); CHECK(same(actual, reference));
    synth->allSoundOff(3); render(*synth, actual); CHECK(peak(actual, historyFrames(rate)) == 0);
  }
}

void pendingRestartAndSameBoundaryOrder() {
  for (double rate : rates) for (auto source : sources) {
    auto synth = instrument(source, rate), fresh = instrument(source, rate);
    Stereo shortBlock(37), actual(2053), reference(2053);
    synth->noteOn(69, 127, 3); render(*synth, shortBlock); render(*fresh, shortBlock);
    synth->noteOn(69, 127, 3); // Existing audition now has a deferred restart.
    synth->allSoundOff(3);    // NoteOn then CC120 at one boundary means silence.
    render(*synth, actual); render(*fresh, reference);
    CHECK(peak(actual, historyFrames(rate)) == 0);
    synth->allSoundOff(3); synth->noteOn(72, 127, 3); // CC120 then fresh NoteOn survives.
    fresh->noteOn(72, 127, 3);
    render(*synth, actual); render(*fresh, reference); CHECK(same(actual, reference));
    CHECK(synth->activeVoiceCount() == 1 && peak(actual) > .0001f);
  }
}

void invalidAndNoTargetAreExactNoOps() {
  for (double rate : rates) for (auto source : sources) {
    auto synth = instrument(source, rate), untouched = instrument(source, rate);
    Stereo actual(1031), reference(1031);
    const ReverbSettings reverb{ReverbPreset::Room, true, false, .75, .4};
    for (auto* current : {synth.get(), untouched.get()}) {
      CHECK(current->setReverb(reverb));
      current->setSustain(true, 3); current->setPitchBend(0, 3);
      current->noteOn(69, 127, 3); current->noteOn(64, 100, 8);
      current->noteOff(69, 3);
    }
    render(*synth, actual); render(*untouched, reference); CHECK(same(actual, reference));
    for (int channel : {-1, 16, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(), 0, 15}) {
      const auto frame = synth->nativeFramesRendered(); synth->allSoundOff(channel);
      CHECK(synth->activeVoiceCount() == 1 && synth->nativeFramesRendered() == frame);
      render(*synth, actual, 1); render(*untouched, reference, 127); CHECK(same(actual, reference));
    }
    synth->setSustain(false, 3); untouched->setSustain(false, 3);
    render(*synth, actual); render(*untouched, reference); CHECK(same(actual, reference));
  }
}

void sharedReverbHistoryAndRatePhaseSurvive() {
  for (double rate : rates) {
    SpuInstrument synth, untouched;
    const ReverbSettings reverb{ReverbPreset::Room, true, false, 1., .6};
    for (auto* current : {&synth, &untouched}) {
      CHECK(current->prepare(rate) && current->setReverb(reverb));
      current->noteOn(69, 127, 3); current->noteOn(60, 83, 9);
    }
    Stereo fill(16387), actual(4099), reference(4099);
    render(synth, fill); render(untouched, fill);
    synth.noteOff(69, 3); untouched.noteOff(69, 3);
    render(synth, actual); render(untouched, reference); CHECK(same(actual, reference));
    // Target3 has finished its fast dry release, but its shared wet history and
    // channel9 continue. Muting3 must equal a completely untouched continuation.
    const auto frame = synth.nativeFramesRendered(); synth.allSoundOff(3);
    CHECK(synth.activeVoiceCount() == 1 && synth.nativeFramesRendered() == frame);
    render(synth, actual, 1); render(untouched, reference, 127);
    CHECK(same(actual, reference) && peak(actual) > .001f);
    synth.noteOff(60, 9); untouched.noteOff(60, 9);
    render(synth, actual); render(untouched, reference); CHECK(same(actual, reference));
    // Once every dry voice is gone this equality still contains an audible wet
    // tail. Calling CC120 on either released identity cannot clear that tail.
    synth.allSoundOff(9); render(synth, actual); render(untouched, reference);
    CHECK(same(actual, reference) && peak(actual) > .00001f);
  }
}

void allocatorAgeAndChannelIdentitySurvive() {
  for (auto source : sources) {
    auto synth = instrument(source, 44100.);
    const int limit = capacity(source);
    synth->noteOn(40, 100, 0); synth->noteOn(41, 100, 1); synth->noteOn(42, 100, 2);
    synth->allSoundOff(0); CHECK(synth->activeVoiceCount() == 2);
    for (int i = 0; i < limit - 2; ++i) synth->noteOn(60 + i, 100, 5);
    CHECK(synth->activeVoiceCount() == limit);
    synth->noteOn(110, 100, 7); CHECK(synth->activeVoiceCount() == limit);
    synth->noteOff(41, 1); // The oldest surviving note41 was stolen, not new note60.
    CHECK(synth->activeVoiceCount() == limit);
    synth->noteOff(42, 2); CHECK(synth->activeVoiceCount() == limit - 1);
    synth->allSoundOff(5); CHECK(synth->activeVoiceCount() == 1);
    synth->allSoundOff(7); CHECK(synth->activeVoiceCount() == 0);
    synth->allSoundOff(2); // Released note42 still owns its long envelope tail.
    Stereo silence(1024); render(*synth, silence); CHECK(peak(silence) == 0);
  }
}

void partitionAndCallbackMemory() {
  static_assert(noexcept(std::declval<SpuInstrument&>().allSoundOff(0)));
  for (double rate : rates) for (auto source : sources) {
    auto synth = instrument(source, rate), partitioned = instrument(source, rate);
    Stereo whole(1031), parts(1031);
    allocations = releases = 0; tracking = true;
    bool valid = synth->prepare(rate) && partitioned->prepare(rate);
    for (unsigned stage = 0; stage < 48; ++stage) {
      for (auto* current : {synth.get(), partitioned.get()}) {
        const int channel = stage % 16;
        current->setPitchBend((stage * 701) % 16384, channel);
        current->setSustain(stage % 3 != 0, channel);
        current->noteOn(36 + stage % 48, 100, channel);
        if (stage % 2) current->noteOff(36 + stage % 48, channel);
        current->allSoundOff((stage + 7) % 16);
        if (stage % 5 == 0) current->allSoundOff(channel);
        current->allSoundOff(-1); current->allSoundOff(16);
      }
      render(*synth, whole, 1031); render(*partitioned, parts, 7); CHECK(same(whole, parts));
    }
    for (int channel = 0; channel < 16; ++channel) {
      synth->allSoundOff(channel); partitioned->allSoundOff(channel);
    }
    render(*synth, whole); render(*partitioned, parts);
    tracking = false;
    CHECK(valid && allocations == 0 && releases == 0);
    CHECK(same(whole, parts) && peak(whole, historyFrames(rate)) == 0);
  }
}
}
int main() {
  stopsHeldDeferredAndReleased();
  unaffectedChannelMatchesIndependentVoice();
  retainedControllersAndFreshReuse();
  pendingRestartAndSameBoundaryOrder();
  invalidAndNoTargetAreExactNoOps();
  sharedReverbHistoryAndRatePhaseSurvive();
  allocatorAgeAndChannelIdentitySurvive();
  partitionAndCallbackMemory();
  std::puts("PASS channel All Sound Off: held/deferred/release; exact independent other-channel PCM; retained bend/pedal; fresh physical reuse/no audition ghosts; same-boundary order; invalid/empty no-op; shared Reverb/rate history; allocator age; PCM/VAB/Noise/PMON/Morph/Software at44.1/48/96k; partition; callback new/delete0");
}
