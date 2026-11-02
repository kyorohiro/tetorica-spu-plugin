// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace tetorica {

inline constexpr double kMaximumAudioGain = 3.8;

// Parameter zero keeps its original 0..100 percent host mapping. A separate
// multiplier extends the audible range without changing old automation.
inline double effectiveAudioGain(double gainPercent, double boost = 1.) noexcept {
  if (!std::isfinite(gainPercent) || !std::isfinite(boost) || gainPercent <= 0.)
    return 0.;
  return std::clamp(gainPercent, 0., 100.) / 100. *
         std::clamp(boost, 1., kMaximumAudioGain);
}

// Audio-thread-owned output gain. This live interpolation is not preset data.
// One next() call represents one host output frame, shared by both channels.
class AudioGainSmoother final {
public:
  void reset(double sampleRate, double gain) noexcept {
    mRampSamples = rampSamples(sampleRate);
    mCurrent = mTarget = safeGain(gain);
    mStep = 0.;
    mRemaining = 0;
  }

  void setTarget(double gain) noexcept {
    gain = safeGain(gain);
    if (gain == mTarget)
      return;
    mTarget = gain;
    mRemaining = mCurrent == mTarget ? 0 : mRampSamples;
    mStep = mRemaining ? (mTarget - mCurrent) / mRampSamples : 0.;
  }

  double next() noexcept {
    if (mRemaining) {
      if (--mRemaining == 0) {
        mCurrent = mTarget;
        mStep = 0.;
      } else {
        // Bound accumulated rounding even for unusual, very long ramps.
        mCurrent = mStep > 0. ? std::min(mTarget, mCurrent + mStep)
                             : std::max(mTarget, mCurrent + mStep);
      }
    }
    return mCurrent;
  }

  double current() const noexcept { return mCurrent; }
  double target() const noexcept { return mTarget; }
  std::uint32_t rampRemaining() const noexcept { return mRemaining; }

private:
  static double safeGain(double gain) noexcept {
    if (!std::isfinite(gain) || gain <= 0.)
      return 0.;
    return std::min(gain, kMaximumAudioGain);
  }

  static std::uint32_t rampSamples(double sampleRate) noexcept {
    constexpr double seconds = .005;
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    // Reject invalid or unrepresentable rates before any integer conversion.
    // Tiny positive rates need one sample; unsupported extremes use 44.1 kHz.
    if (!std::isfinite(sampleRate) || sampleRate <= 0. ||
        sampleRate > static_cast<double>(maximum) / seconds)
      sampleRate = 44100.;
    const double samples = std::ceil(sampleRate * seconds);
    if (!std::isfinite(samples) || samples > static_cast<double>(maximum))
      return 221;
    return static_cast<std::uint32_t>(std::max(1., samples));
  }

  std::uint32_t mRampSamples = 221;
  std::uint32_t mRemaining = 0;
  double mCurrent = 1.;
  double mTarget = 1.;
  double mStep = 0.;
};

} // namespace tetorica
