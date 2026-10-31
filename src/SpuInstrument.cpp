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
// Preset register constants by pSXAuthor/R. Belmont, BSD-3-Clause, retained
// from the pinned SDK's spu_tables.cpp. Its Echo/Delay entries have no effect
// configuration and are deliberately not exposed. This is the native effect.
constexpr std::array<std::array<std::uint16_t,32>,8> kReverbRegisters{{
  {},
  {{0x007d,0x005b,0x6d80,0x54b8,0xbed0,0,0,0xba80,
    0x5800,0x5300,0x04d6,0x0333,0x03f0,0x0227,0x0374,0x01ef,
    0x0334,0x01b5,0,0,0,0,0,0,0,0,0x01b4,0x0136,0x00b8,0x005c,0x8000,0x8000}},
  {{0x0033,0x0025,0x70f0,0x4fa8,0xbce0,0x4410,0xc0f0,0x9c00,
    0x5280,0x4ec0,0x03e4,0x031b,0x03a4,0x02af,0x0372,0x0266,
    0x031c,0x025d,0x025c,0x018e,0x022f,0x0135,0x01d2,0x00b7,
    0x018f,0x00b5,0x00b4,0x0080,0x004c,0x0026,0x8000,0x8000}},
  {{0x00b1,0x007f,0x70f0,0x4fa8,0xbce0,0x4510,0xbef0,0xb4c0,
    0x5280,0x4ec0,0x0904,0x076b,0x0824,0x065f,0x07a2,0x0616,
    0x076c,0x05ed,0x05ec,0x042e,0x050f,0x0305,0x0462,0x02b7,
    0x042f,0x0265,0x0264,0x01b2,0x0100,0x0080,0x8000,0x8000}},
  {{0x00e3,0x00a9,0x6f60,0x4fa8,0xbce0,0x4510,0xbef0,0xa680,
    0x5680,0x52c0,0x0dfb,0x0b58,0x0d09,0x0a3c,0x0bd9,0x0973,
    0x0b59,0x08da,0x08d9,0x05e9,0x07ec,0x04b0,0x06ef,0x03d2,
    0x05ea,0x031d,0x031c,0x0238,0x0154,0x00aa,0x8000,0x8000}},
  {{0x01a5,0x0139,0x6000,0x5000,0x4c00,0xb800,0xbc00,0xc000,
    0x6000,0x5c00,0x15ba,0x11bb,0x14c2,0x10bd,0x11bc,0x0dc1,
    0x11c0,0x0dc3,0x0dc0,0x09c1,0x0bc4,0x07c1,0x0a00,0x06cd,
    0x09c2,0x05c1,0x05c0,0x041a,0x0274,0x013a,0x8000,0x8000}},
  {{0x033d,0x0231,0x7e00,0x5000,0xb400,0xb000,0x4c00,0xb000,
    0x6000,0x5400,0x1ed6,0x1a31,0x1d14,0x183b,0x1bc2,0x16b2,
    0x1a32,0x15ef,0x15ee,0x1055,0x1334,0x0f2d,0x11f6,0x0c5d,
    0x1056,0x0ae1,0x0ae0,0x07a2,0x0464,0x0232,0x8000,0x8000}},
  {{0x0017,0x0013,0x70f0,0x4fa8,0xbce0,0x4510,0xbef0,0x8500,
    0x5f80,0x54c0,0x0371,0x02af,0x02e5,0x01df,0x02b0,0x01d7,
    0x0358,0x026a,0x01d6,0x011e,0x012d,0x00b1,0x011f,0x0059,
    0x01a0,0x00e3,0x0058,0x0040,0x0028,0x0014,0x8000,0x8000}}
}};
bool validChannel(int channel) { return channel >= 0 && channel < 16; }
vab::PreparedTone cycleTone(const PcmCycle& pcm) {
  const auto adpcm=encodePcmCycle(pcm);
  vab::PreparedTone tone;
  tone.adpcm.assign(adpcm.begin(),adpcm.end());
  tone.rootFrequencyHz=44100./112;tone.decodedFrames=112;tone.loopStartSamples=0;
  tone.adsr1=0x000f;tone.adsr2=0x1fc0;tone.volumeLeft=tone.volumeRight=1;
  return tone;
}

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
SpuInstrument::SpuInstrument(const PcmCycle& pcm) : SpuInstrument(cycleTone(pcm)) {}
SpuInstrument::~SpuInstrument() { ps1_spu_destroy(mChip); }

bool SpuInstrument::write(unsigned offset, unsigned value) noexcept {
  const bool ok = ps1_spu_write(mChip, offset, static_cast<std::uint16_t>(value)) != 0;
  mHealthy = mHealthy && ok;
  return ok;
}
bool SpuInstrument::renderNative(float* left, float* right, unsigned count) noexcept {
  if(count&&mReverb.preset!=ReverbPreset::Off)mReverbMayHaveTail=true;
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
  mSourceReverbSend=tone.reverb;
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
  clearReverbTail();
  clearRateHistory();
}
void SpuInstrument::clearReverbTail() noexcept {
  if(!mReverbMayHaveTail)return;
  const bool ok=ps1_spu_clear_reverb(mChip)!=0;
  mHealthy=ok&&mHealthy;
  if(ok)mReverbMayHaveTail=false;
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
bool SpuInstrument::setReverb(ReverbSettings settings) noexcept {
  const auto preset=static_cast<std::uint32_t>(settings.preset);
  if(preset>=kReverbRegisters.size()||!std::isfinite(settings.volumeLeft)||
      settings.volumeLeft<0||settings.volumeLeft>1||!std::isfinite(settings.volumeRight)||
      settings.volumeRight<0||settings.volumeRight>1)return false;
  if(settings.preset==mReverb.preset&&settings.enabled==mReverb.enabled&&
      settings.useToneSend==mReverb.useToneSend&&settings.volumeLeft==mReverb.volumeLeft&&
      settings.volumeRight==mReverb.volumeRight)return mHealthy;
  if(settings.preset!=mReverb.preset){
    for(unsigned i=0;i<32;++i)write(0x1c0+i*2,kReverbRegisters[preset][i]);
    if(settings.preset==ReverbPreset::Off)clearReverbTail();
  }
  const bool enabled=settings.enabled&&settings.preset!=ReverbPreset::Off;
  write(0x184,enabled?static_cast<unsigned>(std::lround(settings.volumeLeft*0x7fff)):0);
  write(0x186,enabled?static_cast<unsigned>(std::lround(settings.volumeRight*0x7fff)):0);
  mReverbSend=enabled&&(!settings.useToneSend||mSourceReverbSend);
  write(0x198,mReverbSend?0xffff:0);write(0x19a,mReverbSend?0x00ff:0);
  write(0x1aa,(ps1_spu_read(mChip,0x1aa)&~0x80u)|(enabled?0x80u:0));
  mReverb=settings;
  return mHealthy;
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
