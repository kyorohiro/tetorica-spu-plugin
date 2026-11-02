// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "../plugin/KeyboardPerformanceState.h"

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
using tetorica::ui::KeyboardOctaveState;
using tetorica::ui::PitchWheelState;

void octaveRanges() {
  KeyboardOctaveState state;
  require(state.octave() == 0 && state.minNote() == 48 && state.maxNote() == 84,
          "Default keyboard range changed");
  require(!state.setOctave(0), "Unchanged octave reported a range change");
  constexpr std::array<int, 8> lower{{0, 12, 24, 36, 48, 60, 72, 84}};
  constexpr std::array<int, 8> upper{{36, 48, 60, 72, 84, 96, 108, 120}};
  for (int octave = KeyboardOctaveState::minOctave;
       octave <= KeyboardOctaveState::maxOctave; ++octave) {
    require(state.setOctave(octave), "Distinct octave did not change range");
    const auto index = static_cast<std::size_t>(octave - KeyboardOctaveState::minOctave);
    require(state.octave() == octave && state.minNote() == lower[index] &&
            state.maxNote() == upper[index], "Octave does not cover the expected 37 MIDI keys");
    require(state.minNote() >= 0 && state.maxNote() <= 127,
            "Keyboard range escaped MIDI bounds");
    require(!state.setOctave(octave), "Repeated octave reported a change");
  }
  require(!state.setOctave(std::numeric_limits<int>::max()) && state.maxNote() == 120,
          "Upper octave clamp overflowed or changed an already clamped range");
  require(state.setOctave(std::numeric_limits<int>::min()) && state.minNote() == 0,
          "Lower octave clamp overflowed or failed to change the range");
  require(!state.setOctave(-5) && state.octave() == KeyboardOctaveState::minOctave,
          "Repeated lower clamp reported a change");
  KeyboardOctaveState independent;
  independent.setOctave(1);
  require(state.minNote() == 0 && independent.minNote() == 60,
          "Keyboard instances share their octave");
}

void gestureLifecycle() {
  PitchWheelState wheel;
  require(wheel.value() == 8192 && !wheel.active(), "Initial wheel is not idle and centered");
  require(!wheel.end() && !wheel.move(0.), "Idle wheel sent MIDI");
  require(wheel.begin(.5) == 8192 && wheel.active(),
          "Centered gesture did not explicitly override a possible external bend");
  require(!wheel.move(.5), "Unchanged gesture position sent redundant MIDI");
  require(wheel.move(0.) == 0 && wheel.value() == 0,
          "Gesture did not reach negative MIDI endpoint");
  require(wheel.move(1.) == 16383 && wheel.value() == 16383,
          "Gesture did not reach positive MIDI endpoint");
  require(wheel.move(.25) == 4096 && wheel.move(.75) == 12288,
          "Wheel quarter positions are not symmetric about exact neutral");
  require(wheel.end() == 8192 && !wheel.active() && wheel.value() == 8192,
          "Release or cancellation failed to return exactly to center");
  require(!wheel.end() && !wheel.move(1.), "Completed gesture sent stale MIDI");
  require(wheel.begin(.5) == 8192 && wheel.end() == 8192,
          "A gesture that stayed centered did not publish its release");
  require(wheel.begin(1.) == 16383, "New gesture failed after an end");
  wheel.reset();
  require(!wheel.active() && wheel.value() == 8192 && !wheel.end() && !wheel.move(0.),
          "Host restore reset left a pending gesture or release event");
  wheel.reset();
  require(!wheel.end(), "Repeated reset manufactured a center MIDI event");
}

void finiteInputAndIsolation() {
  PitchWheelState wheel;
  constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
  constexpr auto infinity = std::numeric_limits<double>::infinity();
  for (const auto invalid : {nan, infinity, -infinity}) {
    require(!wheel.begin(invalid) && !wheel.active() && wheel.value() == 8192,
            "Invalid coordinate started a wheel gesture");
  }
  require(wheel.begin(-std::numeric_limits<double>::max()) == 0,
          "Finite position below the wheel did not clamp to the lower endpoint");
  for (const auto invalid : {nan, infinity, -infinity}) {
    require(!wheel.move(invalid) && wheel.active() && wheel.value() == 0,
            "Invalid move changed the last valid MIDI bend");
    require(!wheel.begin(invalid) && wheel.active() && wheel.value() == 0,
            "Invalid repeated press destroyed an active gesture");
  }
  require(wheel.move(std::numeric_limits<double>::max()) == 16383,
          "Finite position above the wheel did not clamp to the upper endpoint");
  require(!wheel.move(2.), "Equivalent clamped position sent redundant MIDI");
  PitchWheelState independent;
  require(independent.begin(0.) == 0 && independent.active(), "Independent gesture failed");
  require(wheel.value() == 16383 && wheel.active(), "A second instance changed the first bend");
  independent.end();
  require(wheel.value() == 16383 && wheel.active(), "A second instance released the first gesture");
  require(wheel.end() == 8192, "First instance lost its pending release");
  require(wheel.begin(-0.) == 0 && wheel.end() == 8192, "Negative zero is not a valid endpoint");
}

void entireMidiRange() {
  PitchWheelState wheel;
  std::array<bool, 16384> reached{};
  require(wheel.begin(0.) == 0, "Range sweep did not start at zero");
  reached[0] = true;
  int previous = 0;
  std::size_t changes = 0;
  // A dense visual sweep checks every MIDI code is reachable in increasing
  // order, without using a duplicate coordinate-to-MIDI conversion as oracle.
  for (int step = 1; step <= 65536; ++step) {
    const auto emitted = wheel.move(static_cast<double>(step) / 65536.);
    const int value = wheel.value();
    require(value >= previous && value <= 16383, "Wheel mapping is not bounded and monotonic");
    require(value - previous <= 1, "Wheel mapping skips a MIDI code");
    require(bool(emitted) == (value != previous), "Gesture emission differs from MIDI value changes");
    if (emitted) {
      require(*emitted == value, "Gesture returned a different MIDI value than it retained");
      ++changes;
    }
    reached[static_cast<std::size_t>(value)] = true;
    previous = value;
  }
  require(previous == 16383 && changes == 16383, "Sweep did not reach the complete 14-bit range");
  for (const bool value : reached) require(value, "A 14-bit MIDI value is unreachable");
  require(wheel.end() == 8192, "Sweep release did not restore neutral bend");
}
} // namespace

int main() {
  try {
    static_assert(noexcept(std::declval<KeyboardOctaveState&>().setOctave(0)));
    static_assert(noexcept(std::declval<PitchWheelState&>().begin(0.)));
    static_assert(noexcept(std::declval<PitchWheelState&>().move(0.)));
    static_assert(noexcept(std::declval<PitchWheelState&>().end()));
    static_assert(noexcept(std::declval<PitchWheelState&>().reset()));
    static_assert(std::is_trivially_copyable_v<KeyboardOctaveState>);
    static_assert(std::is_trivially_copyable_v<PitchWheelState>);
    octaveRanges();
    gestureLifecycle();
    finiteInputAndIsolation();
    entireMidiRange();
    std::cout << "PASS keyboard performance octaveBounds=true exactCenter=true all14BitValues=true "
                 "gestureCancel=true idleNoMidi=true invalidAtomic=true instanceIsolation=true\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL keyboard performance: " << error.what() << '\n';
    return 1;
  }
}
