// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include <tetorica/ps1_spu.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tetorica {
namespace {
constexpr unsigned kSampleAddress = 0x2000;
constexpr unsigned kCycleFrames = 112;
constexpr unsigned kVoiceVolume = 0x0c00;

// Original 0.8 * sin(2*pi*n/112), Float32 PCM, encoded with our BSD-3-Clause
// encodePs1AdpcmCycle (hello_ymfm/docs/js/ps1_adpcm_encoder.js). Its cyclic
// history scoring avoids the first-packet flattening of independent encoding.
// No game recording or sample bank. Raw second-pass SNR: 60.12 dB.
constexpr std::array<std::uint8_t, 64> kSineAdpcm{{
  38,6,68,85,85,102,117,118,102,119,119,118,103,103,87,102,
  38,0,70,70,69,52,36,35,18,17,0,224,224,222,222,220,
  38,0,188,188,171,171,170,154,154,169,153,169,153,170,169,155,
  38,3,172,187,204,204,237,221,255,14,240,2,33,34,51,67
}};
bool validChannel(int channel) { return channel >= 0 && channel < 16; }
}

SpuInstrument::SpuInstrument() : mChip(ps1_spu_create()) {
  if (!mChip) throw std::runtime_error("Could not create Native SPU");
  try { initialize(); }
  catch (...) { ps1_spu_destroy(mChip); mChip = nullptr; throw; }
}
SpuInstrument::~SpuInstrument() { ps1_spu_destroy(mChip); }

bool SpuInstrument::write(unsigned offset, unsigned value) noexcept {
  const bool ok = ps1_spu_write(mChip, offset, static_cast<std::uint16_t>(value)) != 0;
  mHealthy = mHealthy && ok;
  return ok;
}
bool SpuInstrument::renderNative(float* left, float* right, unsigned count) noexcept {
  const bool ok = ps1_spu_render(mChip, left, right, count) != 0;
  mHealthy = mHealthy && ok;
  if (ok) mNativeFrames += count;
  else { std::fill_n(left, count, 0); std::fill_n(right, count, 0); }
  return ok;
}
void SpuInstrument::initialize() {
  if (ps1_spu_sample_rate() != kNativeSampleRate ||
      !ps1_spu_set_mix_headroom(mChip, 4) ||
      !ps1_spu_load_ram(mChip, kSampleAddress, kSineAdpcm.data(), kSineAdpcm.size()))
    throw std::runtime_error("Could not prepare Native SPU sine");
  write(0x180, 0x3fff); write(0x182, 0x3fff); write(0x1aa, 0xc000);
  for (unsigned voice = 0; voice < kVoiceCount; ++voice) {
    const unsigned base = voice * 16;
    write(base, 0); write(base+2, 0); write(base+4, 0x1000);
    write(base+6, kSampleAddress/8); write(base+8, 0x000f);
    write(base+10, 0x1fc0); write(base+14, kSampleAddress/8);
  }
  // RAM key-on and loop transitions lazily allocate inside the SDK. Exercise
  // every voice and several complete loops now, keeping this immutable sample
  // cache alive afterward. Changing samples/resetting the SDK invalidates this
  // guarantee and must be prepared off the audio thread in a future bank loader.
  write(0x188, 0xffff); write(0x18a, 0x00ff);
  renderNative(mNativeLeft.data(), mNativeRight.data(), 1024);
  write(0x18c, 0xffff); write(0x18e, 0x00ff);
  renderNative(mNativeLeft.data(), mNativeRight.data(), 1024);
  panic();
  mNativeFrames = 0;
  if (!mHealthy) throw std::runtime_error("Could not prime Native SPU caches");
}
void SpuInstrument::clearRateHistory() noexcept {
  mPhase = 0; mRateInitialized = false;
  mPreviousLeft = mPreviousRight = mCurrentLeft = mCurrentRight = 0;
}
bool SpuInstrument::prepare(double hostSampleRate) noexcept {
  if (!std::isfinite(hostSampleRate) || hostSampleRate < 8000 || hostSampleRate > 384000) return false;
  panic();
  mHostRate = hostSampleRate;
  mRatio = kNativeSampleRate / hostSampleRate;
  return mHealthy;
}
void SpuInstrument::noteOn(int note, int velocity, int channel) noexcept {
  if (note < 0 || note > 127 || velocity < 0 || velocity > 127 || !validChannel(channel)) return;
  if (velocity == 0) { noteOff(note, channel); return; }
  int slot = -1;
  // A repeated MIDI note retriggers its existing slot. Other channels retain
  // independent identities; late Off for a stolen note cannot stop its successor.
  for (int i=0; i<kVoiceCount; ++i)
    if (mVoices[i].held && mVoices[i].note == note && mVoices[i].channel == channel) { slot=i; break; }
  if (slot < 0) for (unsigned offset=0; offset<kVoiceCount; ++offset) {
    const unsigned candidate = (mCursor+offset)%kVoiceCount;
    if (!mVoices[candidate].held) { slot=static_cast<int>(candidate); break; }
  }
  if (slot < 0) {
    slot = 0;
    for (int i=1; i<kVoiceCount; ++i) if (mVoices[i].order < mVoices[slot].order) slot=i;
  }
  const double frequency = 440 * std::exp2((note-69)/12.0);
  const auto pitch = static_cast<unsigned>(std::clamp(std::lround(4096*frequency/(44100.0/kCycleFrames)), 1l, 0x3fffl));
  const unsigned volume = kVoiceVolume * static_cast<unsigned>(velocity) / 127;
  const unsigned base = static_cast<unsigned>(slot)*16;
  write(base, volume); write(base+2, volume); write(base+4, pitch);
  mHealthy = (ps1_spu_audition_key(mChip, static_cast<unsigned>(slot), 1) != 0) && mHealthy;
  mVoices[slot] = {note, channel, true, ++mOrder};
  mCursor = (static_cast<unsigned>(slot)+1)%kVoiceCount;
}
void SpuInstrument::noteOff(int note, int channel) noexcept {
  if (note < 0 || note > 127 || !validChannel(channel)) return;
  for (unsigned i=0; i<kVoiceCount; ++i) {
    auto& voice = mVoices[i];
    if (voice.held && voice.note == note && voice.channel == channel) {
      mHealthy = (ps1_spu_audition_key(mChip, i, 0) != 0) && mHealthy;
      voice.held = false;
    }
  }
}
void SpuInstrument::allNotesOff(int channel) noexcept {
  if (channel != -1 && !validChannel(channel)) return;
  for (unsigned i=0; i<kVoiceCount; ++i) {
    auto& voice = mVoices[i];
    if (voice.held && (channel == -1 || voice.channel == channel)) {
      mHealthy = (ps1_spu_audition_key(mChip, i, 0) != 0) && mHealthy;
      voice.held = false;
    }
  }
}
void SpuInstrument::panic() noexcept {
  write(0x18c, 0xffff); write(0x18e, 0x00ff);
  for (unsigned i=0; i<kVoiceCount; ++i) {
    write(i*16, 0); write(i*16+2, 0);
    mVoices[i] = Voice{};
  }
  mCursor=0; mOrder=0;
  clearRateHistory();
}
int SpuInstrument::activeVoiceCount() const noexcept {
  int count=0;
  for (const auto& voice : mVoices) if (voice.held) ++count;
  return count;
}
void SpuInstrument::render(float* left, float* right, int frames) noexcept {
  if (!left || !right || frames <= 0) return;
  if (!mHealthy) { std::fill_n(left, frames, 0); std::fill_n(right, frames, 0); return; }
  if (mHostRate == kNativeSampleRate) {
    for (int at=0; at<frames;) {
      const unsigned count = static_cast<unsigned>(std::min(frames-at, 1024));
      renderNative(left+at, right+at, count);
      at += static_cast<int>(count);
    }
    return;
  }
  for (int at=0; at<frames;) {
    const int count = std::min(frames-at, 128);
    // Count only samples whose time has arrived. Never render a whole future
    // block across a MIDI boundary: rate history remains continuous between
    // arbitrary host partitions and changes take effect at the next native tick.
    double planPhase = mPhase;
    unsigned needed = mRateInitialized ? 0 : 1;
    for (int i=0; i<count; ++i) {
      while (planPhase >= 1) { ++needed; planPhase -= 1; }
      planPhase += mRatio;
    }
    renderNative(mNativeLeft.data(), mNativeRight.data(), needed);
    unsigned read=0;
    if (!mRateInitialized) {
      mPreviousLeft=mCurrentLeft=mNativeLeft[read];
      mPreviousRight=mCurrentRight=mNativeRight[read++];
      mRateInitialized=true;
    }
    for (int i=0; i<count; ++i) {
      while (mPhase >= 1) {
        mPreviousLeft=mCurrentLeft; mPreviousRight=mCurrentRight;
        mCurrentLeft=mNativeLeft[read]; mCurrentRight=mNativeRight[read++];
        mPhase -= 1;
      }
      left[at+i] = mPreviousLeft + static_cast<float>(mPhase)*(mCurrentLeft-mPreviousLeft);
      right[at+i] = mPreviousRight + static_cast<float>(mPhase)*(mCurrentRight-mPreviousRight);
      mPhase += mRatio;
    }
    at += count;
  }
}

} // namespace tetorica
