// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace tetorica::ui {

// Editor-only performance controls. These are neither host parameters nor
// instrument state. The caller releases a touched key before changing range.
class KeyboardOctaveState final {
public:
  static constexpr int minOctave = -4;
  static constexpr int maxOctave = 3;

  int octave() const noexcept { return mOctave; }
  int minNote() const noexcept { return 48 + 12 * mOctave; }
  int maxNote() const noexcept { return minNote() + 36; }

  bool setOctave(int octave) noexcept {
    octave = std::clamp(octave, minOctave, maxOctave);
    if (octave == mOctave) return false;
    mOctave = octave;
    return true;
  }

private:
  int mOctave = 0;
};

// One local pointer gesture owns the wheel until end(). Values returned by
// begin/move/end are MIDI to send through the existing UI FIFO. Merely opening
// an editor, ending an idle gesture or resetting after restore sends nothing.
class PitchWheelState final {
public:
  int value() const noexcept { return mValue; }
  bool active() const noexcept { return mActive; }

  std::optional<int> begin(double position) noexcept {
    if (!std::isfinite(position)) return std::nullopt;
    mValue = valueFor(position);
    mActive = true;
    // Always publish the initial position: a DAW MIDI event may have changed
    // the channel's real bend while this editor model remained at its center.
    return mValue;
  }

  std::optional<int> move(double position) noexcept {
    if (!mActive || !std::isfinite(position)) return std::nullopt;
    const int next = valueFor(position);
    if (next == mValue) return std::nullopt;
    mValue = next;
    return mValue;
  }

  std::optional<int> end() noexcept {
    if (!mActive) return std::nullopt;
    reset();
    return mValue;
  }

  void reset() noexcept {
    mValue = 8192;
    mActive = false;
  }

private:
  static int valueFor(double position) noexcept {
    position = std::clamp(position, 0., 1.);
    // MIDI has 8192 negative steps and 8191 positive steps. Give the visual
    // center its exact neutral value while preserving both 14-bit endpoints.
    return static_cast<int>(std::lround(position <= .5
      ? position * 16384.
      : 8192. + (position - .5) * 16382.));
  }

  int mValue = 8192;
  bool mActive = false;
};

} // namespace tetorica::ui
