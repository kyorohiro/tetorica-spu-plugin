// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <cstdint>

namespace tetorica::ui {

enum class SoundPreparationPhase { Idle, Preparing, Ready, Loaded };

// Presentation only. Ready remains pending until the audio thread adopts the
// prepared generation; it neither diagnoses a stopped host nor starts audio.
constexpr SoundPreparationPhase soundPreparationPhase(std::uint64_t request,
  std::uint64_t latest, std::uint64_t applied, bool loading, bool ready) noexcept {
  if (!request || request != latest || (!loading && !ready))
    return SoundPreparationPhase::Idle;
  if (applied == request) return SoundPreparationPhase::Loaded;
  return ready ? SoundPreparationPhase::Ready : SoundPreparationPhase::Preparing;
}

#if defined(APP_API)
inline constexpr const char* kSoundReadyStatus = "Ready - waiting for audio.";
inline constexpr const char* kSoundReadyDetails =
  "Ready. The sound applies when audio resumes. Check audio settings if it stays Ready.";
inline constexpr const char* kSoundReadyInfo =
  "Sound preparation is complete.\nCheck audio settings if it stays Ready.";
#else
inline constexpr const char* kSoundReadyStatus = "Ready - start DAW playback to apply.";
inline constexpr const char* kSoundReadyDetails =
  "Ready. Start DAW playback to apply the sound. No need to press Apply again.";
inline constexpr const char* kSoundReadyInfo =
  "Sound preparation is complete.\nStart DAW playback to apply it.";
#endif

constexpr const char* soundPreparationButtonLabel(SoundPreparationPhase phase) noexcept {
  switch (phase) {
    case SoundPreparationPhase::Preparing: return "Preparing...";
    case SoundPreparationPhase::Ready: return "Ready";
    case SoundPreparationPhase::Loaded: return "Loaded";
    default: return nullptr; // Keep the current draft's manual Apply label.
  }
}

} // namespace tetorica::ui
