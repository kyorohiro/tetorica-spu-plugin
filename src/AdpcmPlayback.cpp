// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "AdpcmPlayback.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace tetorica {
namespace {
void retainCompatibleWaveform(RawAdpcmSample& sample) {
  if(!sample.waveform)return;
  if(sample.baseFrequencyHz!=44100./112||sample.loopStartSamples!=std::optional<std::uint32_t>(0)||sample.adpcm.size()!=64) {
    sample.waveform.reset();return;
  }
  std::array<std::uint8_t,64> encoded{};
  std::copy(sample.adpcm.begin(),sample.adpcm.end(),encoded.begin());
  if(!matchesPcmCycleAdpcm(*sample.waveform,encoded))sample.waveform.reset();
}
}
RawAdpcmSample withAdpcmRoot(const RawAdpcmSample& sample,int midiNote) {
  validateRawAdpcmSample(sample);
  if(midiNote<0||midiNote>127)throw std::invalid_argument("Root MIDI note must be 0..127");
  auto result=sample;
  result.baseFrequencyHz=440.*std::pow(2.,(midiNote-69)/12.);
  retainCompatibleWaveform(result);
  validateRawAdpcmSample(result);
  return result;
}
RawAdpcmSample withAdpcmPlayback(const RawAdpcmSample& sample,bool loopFromStart) {
  validateRawAdpcmSample(sample);
  auto result=sample;
  for(std::size_t offset=0;offset<result.adpcm.size();offset+=16)
    result.adpcm[offset+1]=0;
  if(loopFromStart) {
    result.adpcm[1]=6;
    result.loopStartSamples=0;
  } else result.loopStartSamples.reset();
  result.adpcm[result.adpcm.size()-15]|=loopFromStart?3:1;
  retainCompatibleWaveform(result);
  validateRawAdpcmSample(result);
  return result;
}
} // namespace tetorica
