// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <array>
#include <cstdint>

namespace tetorica {

// One exclusive-owner SPU instance. Construct/destruct away from the audio
// callback: construction loads the original sine and primes its ADPCM caches.
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
  ~SpuInstrument();
  SpuInstrument(const SpuInstrument&) = delete;
  SpuInstrument& operator=(const SpuInstrument&) = delete;

  // RT-safe after construction. Accepts 8--384 kHz; invalid rates do not
  // change the existing instrument. A valid prepare silences all voices and
  // clears only adapter phase/history, retaining the warmed SDK caches.
  bool prepare(double hostSampleRate) noexcept;
  // MIDI0--127 accepted; this112-sample sine saturates the hardware14-bit
  // PITCH register above~1.575kHz (notes91+). Velocity0 is NoteOff.
  void noteOn(int note, int velocity, int channel = 0) noexcept;
  void noteOff(int note, int channel = 0) noexcept;
  void allNotesOff(int channel = -1) noexcept; // Authored release, optional MIDI channel.
  void panic() noexcept;                     // Immediate mute, all MIDI channels.
  void render(float* left, float* right, int frames) noexcept;

  int activeVoiceCount() const noexcept; // Held keys; releasing voices are excluded.
  bool healthy() const noexcept { return mHealthy; }
  double sampleRate() const noexcept { return mHostRate; }
  std::uint64_t nativeFramesRendered() const noexcept { return mNativeFrames; }

private:
  struct Voice {
    int note = -1;
    int channel = 0;
    bool held = false;
    std::uint64_t order = 0;
  };
  bool write(unsigned offset, unsigned value) noexcept;
  bool renderNative(float* left, float* right, unsigned count) noexcept;
  void clearRateHistory() noexcept;
  void initialize();

  void* mChip = nullptr;
  bool mHealthy = true;
  std::array<Voice, kVoiceCount> mVoices{};
  unsigned mCursor = 0;
  std::uint64_t mOrder = 0;
  std::uint64_t mNativeFrames = 0;
  double mHostRate = kNativeSampleRate;
  double mRatio = 1;
  double mPhase = 0;
  bool mRateInitialized = false;
  float mPreviousLeft = 0, mPreviousRight = 0;
  float mCurrentLeft = 0, mCurrentRight = 0;
  std::array<float, 1024> mNativeLeft{}, mNativeRight{};
};

} // namespace tetorica
