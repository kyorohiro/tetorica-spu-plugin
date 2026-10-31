// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "VabBank.h"
#include <tetorica/ps1_spu.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace tetorica {
namespace {
constexpr unsigned kSampleAddress = 0x2000;
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

void validateTone(const vab::PreparedTone& tone) {
  const auto& bytes=tone.adpcm;
  if (bytes.empty() || bytes.size()%16 || bytes.size()>vab::kMaxSampleBytes ||
      tone.decodedFrames!=bytes.size()/16*28)
    throw std::invalid_argument("VAB Tone: invalid ADPCM length or decoded frame count");
  if (!std::isfinite(tone.rootFrequencyHz) || tone.rootFrequencyHz<=0 ||
      !std::isfinite(tone.volumeLeft) || tone.volumeLeft<0 || tone.volumeLeft>1 ||
      !std::isfinite(tone.volumeRight) || tone.volumeRight<0 || tone.volumeRight>1)
    throw std::invalid_argument("VAB Tone: invalid tuning or volume");
  if (tone.loopStartSamples && (*tone.loopStartSamples>=tone.decodedFrames || *tone.loopStartSamples%28))
    throw std::invalid_argument("VAB Tone: invalid repeat position");
  std::optional<std::uint32_t> marker;
  for (std::size_t at=0; at<bytes.size(); at+=16) {
    const auto flags=bytes[at+1];
    if ((bytes[at]>>4)>4 || (bytes[at]&15)>12 || (flags&~7) ||
        bool(flags&1)!=(at+16==bytes.size()))
      throw std::invalid_argument("VAB Tone: invalid ADPCM packet");
    if (flags&4) {
      if (marker) throw std::invalid_argument("VAB Tone: multiple repeat markers");
      marker=static_cast<std::uint32_t>(at/16*28);
    }
  }
  const bool loop=(bytes[bytes.size()-15]&2)!=0;
  if (loop!=tone.loopStartSamples.has_value() || (marker && marker!=tone.loopStartSamples))
    throw std::invalid_argument("VAB Tone: incoherent repeat metadata");
  // The pinned SDK's repeat-address fallback requires flag2 on that packet.
  // A flag4-only loop start is structurally valid in the JS importer, but its
  // native playback stops at END. Reject that subset, never rewrite its bytes.
  if (loop && !(bytes[(*tone.loopStartSamples/28)*16+1]&2))
    throw std::invalid_argument("VAB Tone: pinned SPU SDK requires repeat flag 2 at the loop start (flag 4 alone is unsupported)");
}
}

SpuInstrument::SpuInstrument() : mChip(ps1_spu_create()) {
  mPitchBend.fill(8192);
  if (!mChip) throw std::runtime_error("Could not create Native SPU");
  try { initialize(); }
  catch (...) { ps1_spu_destroy(mChip); mChip = nullptr; throw; }
}
SpuInstrument::SpuInstrument(const vab::PreparedTone& tone) {
  mPitchBend.fill(8192);
  validateTone(tone);
  mChip=ps1_spu_create();
  if (!mChip) throw std::runtime_error("Could not create Native SPU");
  try { initializeTone(tone); }
  catch (...) { ps1_spu_destroy(mChip); mChip=nullptr; throw; }
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
    write(base+6, kSampleAddress/8); write(base+8, mEnvelope.adsr1);
    write(base+10, mEnvelope.adsr2); write(base+14, kSampleAddress/8);
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
void SpuInstrument::initializeTone(const vab::PreparedTone& tone) {
  constexpr unsigned address=0x1000;
  const unsigned repeat=address+tone.loopStartSamples.value_or(0)/28*16;
  mRootFrequencyHz=tone.rootFrequencyHz;
  mVolumeLeft=static_cast<unsigned>(std::lround(kVoiceVolume*tone.volumeLeft));
  mVolumeRight=static_cast<unsigned>(std::lround(kVoiceVolume*tone.volumeRight));
  mReverbSend=tone.reverb;
  mSourceEnvelope=mEnvelope={tone.adsr1,tone.adsr2};
  if (ps1_spu_sample_rate()!=kNativeSampleRate || !ps1_spu_set_mix_headroom(mChip,4) ||
      !ps1_spu_load_ram(mChip,address,tone.adpcm.data(),static_cast<std::uint32_t>(tone.adpcm.size())))
    throw std::runtime_error("Could not load Native SPU VAB Tone");
  write(0x180,0x3fff); write(0x182,0x3fff); write(0x1aa,0xc000);
  for (unsigned voice=0; voice<kVoiceCount; ++voice) {
    const unsigned base=voice*16;
    write(base,0); write(base+2,0); write(base+4,0x3fff);
    write(base+6,address/8); write(base+8,0x000f);
    write(base+10,0x1fc0); write(base+14,repeat/8);
  }
  write(0x188,0xffff); write(0x18a,0x00ff);
  // Force the first END off-thread, including loops at the far end of a long
  // sample. Temporary held ADSR prevents authored decay from stopping early.
  // No wall-clock sleep: bounded synchronous offline rendering, all voices mute.
  unsigned warmFrames=static_cast<unsigned>((std::uint64_t(tone.decodedFrames)*4096+0x3ffe)/0x3fff)+256;
  while (warmFrames) {
    const unsigned count=std::min(warmFrames,1024u);
    renderNative(mNativeLeft.data(),mNativeRight.data(),count);
    warmFrames-=count;
  }
  // Fast temporary release must finish before restoring an infinite/slow
  // authored release. Otherwise the first real note could fade a priming voice.
  write(0x18c,0xffff); write(0x18e,0x00ff);
  renderNative(mNativeLeft.data(),mNativeRight.data(),1024);
  for (unsigned voice=0; voice<kVoiceCount; ++voice) {
    const unsigned base=voice*16;
    write(base+4,0x1000); write(base+8,mEnvelope.adsr1); write(base+10,mEnvelope.adsr2);
  }
  panic();
  mNativeFrames=0;
  if (!mHealthy) throw std::runtime_error("Could not prime Native SPU VAB caches");
}
void SpuInstrument::clearRateHistory() noexcept {
  mPhase = 0; mRateInitialized = false;
  mPreviousLeft = mPreviousRight = mCurrentLeft = mCurrentRight = 0;
}
bool SpuInstrument::prepare(double hostSampleRate) noexcept {
  if (!std::isfinite(hostSampleRate) || hostSampleRate < 8000 || hostSampleRate > 384000) return false;
  panic();
  resetControllers();
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
    if ((mVoices[i].held||mVoices[i].deferred) && mVoices[i].note == note && mVoices[i].channel == channel) { slot=i; break; }
  if (slot < 0) for (unsigned offset=0; offset<kVoiceCount; ++offset) {
    const unsigned candidate = (mCursor+offset)%kVoiceCount;
    if (!mVoices[candidate].held&&!mVoices[candidate].deferred) { slot=static_cast<int>(candidate); break; }
  }
  if (slot < 0) {
    slot = 0;
    for (int i=1; i<kVoiceCount; ++i) if (mVoices[i].order < mVoices[slot].order) slot=i;
  }
  const auto pitch = pitchFor(note,channel);
  const unsigned volumeLeft = mVolumeLeft * static_cast<unsigned>(velocity) / 127;
  const unsigned volumeRight = mVolumeRight * static_cast<unsigned>(velocity) / 127;
  const unsigned base = static_cast<unsigned>(slot)*16;
  write(base, volumeLeft); write(base+2, volumeRight); write(base+4, pitch);
  write(base+8,mEnvelope.adsr1); write(base+10,mEnvelope.adsr2);
  if (mReverbSend) {
    const unsigned reg=0x198+(slot>=16?2:0);
    write(reg,ps1_spu_read(mChip,reg)|(1u<<(slot%16)));
  }
  mHealthy = (ps1_spu_audition_key(mChip, static_cast<unsigned>(slot), 1) != 0) && mHealthy;
  mVoices[slot] = {note, channel, true, false, ++mOrder};
  mCursor = (static_cast<unsigned>(slot)+1)%kVoiceCount;
}
void SpuInstrument::noteOff(int note, int channel) noexcept {
  if (note < 0 || note > 127 || !validChannel(channel)) return;
  for (unsigned i=0; i<kVoiceCount; ++i) {
    auto& voice = mVoices[i];
    if (voice.held && voice.note == note && voice.channel == channel) {
      voice.held = false;
      if(mSustain[channel])voice.deferred=true;
      else releaseVoice(i);
    }
  }
}
unsigned SpuInstrument::pitchFor(int note,int channel) const noexcept {
  const int delta=mPitchBend[channel]-8192;
  const double semitones=2.0*delta/(delta<0?8192.0:8191.0);
  const double frequency=440*std::exp2((note-69+semitones)/12.0);
  return static_cast<unsigned>(std::lround(std::clamp(4096*frequency/mRootFrequencyHz,1.0,16383.0)));
}
void SpuInstrument::releaseVoice(unsigned voice) noexcept {
  mVoices[voice].deferred=false;
  mHealthy=(ps1_spu_audition_key(mChip,voice,0)!=0)&&mHealthy;
}
void SpuInstrument::setPitchBend(int value14,int channel) noexcept {
  if(!validChannel(channel)||value14<0||value14>16383||mPitchBend[channel]==value14)return;
  mPitchBend[channel]=value14;
  // Released tails retain their note identity until the slot is reused. Change
  // only PITCH, preserving the sample cursor, ADSR stage and key-on history.
  for(unsigned i=0;i<kVoiceCount;++i)
    if(mVoices[i].note>=0&&mVoices[i].channel==channel)
      write(i*16+4,pitchFor(mVoices[i].note,channel));
}
void SpuInstrument::setSustain(bool down,int channel) noexcept {
  if(!validChannel(channel)||mSustain[channel]==down)return;
  mSustain[channel]=down;
  if(!down)for(unsigned i=0;i<kVoiceCount;++i)
    if(mVoices[i].deferred&&mVoices[i].channel==channel)releaseVoice(i);
}
void SpuInstrument::resetControllers(int channel) noexcept {
  if(channel!=-1&&!validChannel(channel))return;
  for(int ch=0;ch<16;++ch)if(channel==-1||ch==channel){
    setPitchBend(8192,ch);
    setSustain(false,ch);
  }
}
void SpuInstrument::allNotesOff(int channel) noexcept {
  if (channel != -1 && !validChannel(channel)) return;
  for (unsigned i=0; i<kVoiceCount; ++i) {
    auto& voice = mVoices[i];
    if (voice.held && (channel == -1 || voice.channel == channel)) {
      voice.held = false;
      if(mSustain[voice.channel])voice.deferred=true;
      else releaseVoice(i);
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
void SpuInstrument::setEnvelope(EnvelopeRegisters values,bool live) noexcept {
  mEnvelope=values;
  for(unsigned i=0;i<kVoiceCount;++i){
    write(i*16+8,values.adsr1); write(i*16+10,values.adsr2);
  }
  if(live)for(unsigned i=0;i<kVoiceCount;++i)
    if(mVoices[i].note>=0)
      mHealthy=(ps1_spu_refresh_envelope(mChip,i)!=0)&&mHealthy;
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
