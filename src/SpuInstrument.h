// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuEnvelope.h"
#include "SpuReverb.h"
#include "SpuWaveform.h"
#include "SpuNoise.h"
#include "SpuPmon.h"
#include "SpuMorph.h"
#include <array>
#include <cstdint>

namespace tetorica {
namespace vab { struct PreparedTone; }
struct RawAdpcmSample;
struct SoftwarePcmSample;

// One exclusive-owner SPU instance. Construct/destruct away from the audio
// callback: construction loads an immutable sample and primes its ADPCM caches.
// The caller serializes MIDI and render calls on its audio thread and splits
// render blocks at MIDI event offsets. No SDK reset or RAM upload runs there.
//
// This first rate adapter uses causal linear interpolation with at most one
// native-sample delay (direct output at 44.1 kHz). It has no antialias low-pass
// filter for hosts below 44.1 kHz; a higher-quality adapter is a follow-up.
class SpuInstrument final {
public:
  static constexpr int kVoiceCount = 24;
  static constexpr int kNativeSampleRate = 44100;

  SpuInstrument();
  // Prepare a validated immutable VAB Tone away from the audio thread. Throws
  // on invalid metadata; retains original packet flags and sample bytes.
  // Imported reverb send is retained, with shared reverb still Off initially.
  explicit SpuInstrument(const vab::PreparedTone& tone);
  // Encode and prime a112-point periodic PCM cycle away from audio. The four
  // ADPCM packets use the ordinary SPU RAM/ADSR/Gaussian/PMON/reverb path.
  explicit SpuInstrument(const PcmCycle& pcm);
  explicit SpuInstrument(const RawAdpcmSample& sample);
  // Owned long mono44100-Hz PCM is copied into the private native bank away
  // from audio. Gaussian/ADSR/PMON/Reverb remain in the SPU render path.
  // Software audio retains authored Morph settings but does not apply them.
  explicit SpuInstrument(const SoftwarePcmSample& sample);
  ~SpuInstrument();
  SpuInstrument(const SpuInstrument&) = delete;
  SpuInstrument& operator=(const SpuInstrument&) = delete;

  // RT-safe after construction. Accepts 8--384 kHz; invalid rates do not
  // change the existing instrument. A valid prepare silences all voices and
  // resets controllers and adapter phase/history, retaining warmed SDK caches.
  bool prepare(double hostSampleRate) noexcept;
  // MIDI0--127 accepted; tuning saturates the hardware14-bit PITCH register.
  // Built-in112-sample sine reaches its limit above~1.575kHz (notes92+).
  // Imported range depends on rootFrequencyHz. Velocity0 is NoteOff.
  void noteOn(int note, int velocity, int channel = 0) noexcept;
  void noteOff(int note, int channel = 0) noexcept;
  // Runtime MIDI controllers are independent per channel and are not authored
  // instrument state. Bend0/8192/16383 maps exactly to -2/0/+2 semitones.
  void setPitchBend(int value14, int channel = 0) noexcept;
  void setSustain(bool down, int channel = 0) noexcept;
  void resetControllers(int channel = -1) noexcept; // Neutral bend; release pedal only.
  void allNotesOff(int channel = -1) noexcept;      // Key releases, respecting pedal.
  void panic() noexcept; // Immediate mute; controllers retained until reset/prepare.
  // The original Tone values never change. Current authored ADSR survives
  // prepare/panic and applies to future Note On, including retriggered voices.
  EnvelopeRegisters sourceEnvelope() const noexcept { return mSourceEnvelope; }
  EnvelopeRegisters envelope() const noexcept { return mEnvelope; }
  // Both raw words reach every voice before an optional live refresh. Live
  // updates preserve current level/sample position, including Release tails;
  // false leaves each running note's latched ADSR until its next Note On.
  // Calling with unchanged words and live=true explicitly applies a deferred
  // edit. No key on/off, sample upload, cache reset or allocation is performed.
  void setEnvelope(EnvelopeRegisters values, bool live = true) noexcept;
  ReverbSettings reverb() const noexcept { return mReverb; }
  bool sourceReverbSend() const noexcept { return mSourceReverbSend; }
  // Shared preset effect. Disabled retains the selected preset while muting
  // returns/sends; hidden delay history continues to decay. Off clears history.
  // All validation precedes mutation. Invalid tuples are ignored and return
  // false. Panic/prepare clear wet tails while preserving authored settings.
  bool setReverb(ReverbSettings settings) noexcept;
  NoiseSettings noise() const noexcept { return mNoise; }
  // Changing mode panics, retaining the authored sample and controllers.
  // Clock-only edits preserve held notes, release, ADSR and the noise sequence.
  // All24 noise voices share the native6-bit clock and ignore musical pitch.
  // Invalid clock values leave the entire instrument unchanged.
  bool setNoise(NoiseSettings settings) noexcept;
  PmonSettings pmon() const noexcept { return mPmon; }
  // Pairing changes panic; enabled PMON permits12 notes. Waveform/depth and
  // ratio edits preserve held/tail phase and envelopes. Decay applies on the
  // next NoteOn only. Enabling PMON disables Noise, and conversely.
  // Invalid tuples leave all settings and voices unchanged.
  bool setPmon(PmonSettings settings) noexcept;
  MorphSettings morph() const noexcept { return mMorph; }
  // Audible cycle topology, distinct from the retained raw host parameter.
  // Noise and Software audio mask Morph without stopping their held notes.
  bool effectiveMorphEnabled() const noexcept { return mMorph.enabled&&!mNoise.enabled&&!mSoftwarePcm; }
  const MorphEndpoints& morphEndpoints() const noexcept { return mMorphEndpoints; }
  // The wrapper adopts endpoint edits on a worker-prepared engine. This fixed
  // copy also preserves voice state if called with serialized audio ownership.
  // Invalid endpoints leave all state unchanged.
  bool setMorphEndpoints(const MorphEndpoints& endpoints) noexcept;
  // Amount updates the private112-frame carrier without key events or ADSR/
  // Gaussian/cursor resets. Effective On/Off changes panic and restore the
  // original source when Off. Noise takes priority and retains raw settings.
  // Software audio retains raw Morph/endpoints with effective Morph Off.
  bool setMorph(MorphSettings settings) noexcept;
  void render(float* left, float* right, int frames) noexcept;

  int activeVoiceCount() const noexcept; // Physically held keys; pedal-held/releasing excluded.
  bool healthy() const noexcept { return mHealthy; }
  double sampleRate() const noexcept { return mHostRate; }
  std::uint64_t nativeFramesRendered() const noexcept { return mNativeFrames; }

private:
  struct Voice {
    int note = -1;
    int channel = 0;
    bool held = false;
    bool deferred = false;
    std::uint64_t order = 0;
  };
  bool write(unsigned offset, unsigned value) noexcept;
  bool renderNative(float* left, float* right, unsigned count) noexcept;
  unsigned pitchFor(int note, int channel) const noexcept;
  unsigned modulatorPitchFor(int note,int channel) const noexcept;
  unsigned carrierFor(unsigned slot) const noexcept;
  void keyOn(unsigned physicalVoice) noexcept;
  void releaseVoice(unsigned voice) noexcept;
  void configureSourceMode(bool noise,bool pmon) noexcept;
  void writeReverbSendMask() noexcept;
  void clearRateHistory() noexcept;
  void clearReverbTail() noexcept;
  void initialize();
  void initializeTone(const vab::PreparedTone& tone);
  void initializeNoiseCache();

  void* mChip = nullptr;
  bool mHealthy = true;
  std::array<Voice, kVoiceCount> mVoices{};
  std::array<bool,kVoiceCount> mFreshSource{};
  std::array<std::uint16_t,kVoiceCount> mSampleRepeatAddresses{};
  std::array<int,16> mPitchBend{};
  std::array<bool,16> mSustain{};
  unsigned mCursor = 0;
  std::uint64_t mOrder = 0;
  std::uint64_t mNativeFrames = 0;
  double mHostRate = kNativeSampleRate;
  double mRootFrequencyHz = kNativeSampleRate / 112.0;
  unsigned mVolumeLeft = 0x0c00, mVolumeRight = 0x0c00;
  bool mInvertLeft=false,mInvertRight=false;
  bool mReverbSend = false;
  bool mSourceReverbSend = false;
  bool mReverbMayHaveTail = false;
  ReverbSettings mReverb{};
  NoiseSettings mNoise{};
  PmonSettings mPmon{};
  bool mPmonWavePrepared = false;
  MorphEndpoints mMorphEndpoints{};
  MorphSettings mMorph{};
  bool mMorphWavePrepared = false;
  bool mSoftwarePcm = false;
  EnvelopeRegisters mSourceEnvelope{}, mEnvelope{};
  double mRatio = 1;
  double mPhase = 0;
  bool mRateInitialized = false;
  float mPreviousLeft = 0, mPreviousRight = 0;
  float mCurrentLeft = 0, mCurrentRight = 0;
  std::array<float, 1024> mNativeLeft{}, mNativeRight{};
};

} // namespace tetorica
