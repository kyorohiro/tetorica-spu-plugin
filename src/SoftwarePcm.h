// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once
#include "SpuEnvelope.h"
#include "VabBank.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tetorica {
inline constexpr std::size_t kMaxSoftwarePcmBytes=32u*1024u*1024u;
inline constexpr std::size_t kMaxSoftwarePcmFrames=kMaxSoftwarePcmBytes/2;
inline constexpr std::size_t kMaxSoftwarePcmNameBytes=320;
// Each factory makes private owned storage, severing caller-held mutable/raw
// aliases. Definitions and metadata edits share this immutable mono 44100-Hz
// buffer. No factory, base64 conversion or destruction belongs on audio.
class SoftwarePcmBuffer final {
public:
  static std::shared_ptr<const SoftwarePcmBuffer> copyOf(const std::vector<std::int16_t>& samples);
  static std::shared_ptr<const SoftwarePcmBuffer> fromLittleEndian(vab::BytesView bytes);
  const std::vector<std::int16_t>& samples() const noexcept{return mSamples;}
  SoftwarePcmBuffer(const SoftwarePcmBuffer&)=delete;
  SoftwarePcmBuffer& operator=(const SoftwarePcmBuffer&)=delete;
private:
  explicit SoftwarePcmBuffer(std::vector<std::int16_t> samples);
  const std::vector<std::int16_t> mSamples;
  friend std::shared_ptr<const SoftwarePcmBuffer> decodeSoftwarePcmBase64(std::string_view text);
};
struct SoftwarePcmSample {
  std::shared_ptr<const SoftwarePcmBuffer> pcm;
  double baseFrequencyHz=261.6255653005986;
  bool loop=false;
  double volumeLeft=.75,volumeRight=.75;
  EnvelopeRegisters envelope{};
  bool reverb=false;
  std::uint16_t inspectorPitch=0x1000;
  std::string name;
};
// O(1) in recording length: buffer factories already prove sample structure.
// Validate positive Web-v2-compatible tuning, signed volumes, names and loop
// minimum4 frames. Invalid input throws without mutating shared authoring data.
void validateSoftwarePcmSample(const SoftwarePcmSample& sample);
bool sameSoftwarePcmSample(const SoftwarePcmSample& a,const SoftwarePcmSample& b) noexcept;
// Strict canonical RFC4648 alphabet/padding, even nonempty PCM16LE bytes, and
// the32MiB decoded bound. Decode directly into one private PCM allocation.
std::shared_ptr<const SoftwarePcmBuffer> decodeSoftwarePcmBase64(std::string_view text);
// Appends canonical base64 directly to a caller's final output string, without
// building another full encoded buffer. Caller handles the enclosing JSON cap.
void appendSoftwarePcmBase64(const SoftwarePcmBuffer& pcm,std::string& output);
} // namespace tetorica
