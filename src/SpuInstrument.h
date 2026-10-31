// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "SpuEnvelope.h"
#include "SpuReverb.h"
#include <array>
#include <cstdint>

namespace tetorica {
namespace vab { struct PreparedTone; }

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
  // on invalid metadata or SDK-unsupported repeat flags; retains original bytes.
  // Imported reverb send is retained, with shared reverb still Off initially.
  explicit SpuInstrument(const vab::PreparedTone& tone);
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
  void releaseVoice(unsigned voice) noexcept;
  void clearRateHistory() noexcept;
  void clearReverbTail() noexcept;
  void initialize();
  void initializeTone(const vab::PreparedTone& tone);

  void* mChip = nullptr;
  bool mHealthy = true;
  std::array<Voice, kVoiceCount> mVoices{};
  std::array<int,16> mPitchBend{};
  std::array<bool,16> mSustain{};
  unsigned mCursor = 0;
  std::uint64_t mOrder = 0;
  std::uint64_t mNativeFrames = 0;
  double mHostRate = kNativeSampleRate;
  double mRootFrequencyHz = kNativeSampleRate / 112.0;
  unsigned mVolumeLeft = 0x0c00, mVolumeRight = 0x0c00;
  bool mReverbSend = false;
  bool mSourceReverbSend = false;
  bool mReverbMayHaveTail = false;
  ReverbSettings mReverb{};
  EnvelopeRegisters mSourceEnvelope{}, mEnvelope{};
  double mRatio = 1;
  double mPhase = 0;
  bool mRateInitialized = false;
  float mPreviousLeft = 0, mPreviousRight = 0;
  float mCurrentLeft = 0, mCurrentRight = 0;
  std::array<float, 1024> mNativeLeft{}, mNativeRight{};
};

} // namespace tetorica
