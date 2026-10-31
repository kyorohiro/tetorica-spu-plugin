// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "TetoricaSPU.h"
#include "InstrumentState.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
using Parameters=std::array<double,kNumParams>;
static_assert(kNumParams==17,"Update the state parameter mapping when adding parameters");
unsigned field(const Parameters& values,int index,unsigned maximum)
{
  const double value=values[index];
  if(!std::isfinite(value)||value<0||value>maximum||std::floor(value)!=value)
    throw std::runtime_error("Invalid authored discrete parameter");
  return static_cast<unsigned>(value);
}
tetorica::EnvelopeSettings envelope(const Parameters& p)
{
  tetorica::EnvelopeSettings result;
  result.enabled=field(p,kParamEnvelopeEnabled,1)!=0;
  result.live=field(p,kParamEnvelopeLive,1)!=0;
  result.custom.adsr1=static_cast<std::uint16_t>(field(p,kParamSustainLevel,15)|
    (field(p,kParamDecayRate,15)<<4)|(field(p,kParamAttackRate,127)<<8)|
    (field(p,kParamAttackExponential,1)<<15));
  result.custom.adsr2=static_cast<std::uint16_t>(field(p,kParamReleaseRate,31)|
    (field(p,kParamReleaseExponential,1)<<5)|(field(p,kParamSustainRate,127)<<6)|
    (field(p,kParamSustainDecrease,1)<<14)|(field(p,kParamSustainExponential,1)<<15));
  return result;
}
tetorica::ReverbSettings reverb(const Parameters& p)
{
  tetorica::ReverbSettings result;
  result.preset=static_cast<tetorica::ReverbPreset>(field(p,kParamReverbPreset,7));
  result.enabled=field(p,kParamReverbEnabled,1)!=0;
  result.useToneSend=field(p,kParamReverbToneSend,1)!=0;
  result.volumeLeft=p[kParamReverbLeft]/100.;result.volumeRight=p[kParamReverbRight]/100.;
  return result; // The pure codec validates finite return ranges before allocation.
}
Parameters stateParameters(const tetorica::SavedInstrumentState& state)
{
  Parameters p{};const auto words=state.envelope.custom;
  p[kParamGain]=state.gainPercent;
  p[kParamEnvelopeEnabled]=state.envelope.enabled?1:0;
  p[kParamEnvelopeLive]=state.envelope.live?1:0;
  p[kParamAttackRate]=(words.adsr1>>8)&127;
  p[kParamAttackExponential]=(words.adsr1>>15)&1;
  p[kParamDecayRate]=(words.adsr1>>4)&15;
  p[kParamSustainLevel]=words.adsr1&15;
  p[kParamSustainRate]=(words.adsr2>>6)&127;
  p[kParamSustainDecrease]=(words.adsr2>>14)&1;
  p[kParamSustainExponential]=(words.adsr2>>15)&1;
  p[kParamReleaseRate]=words.adsr2&31;
  p[kParamReleaseExponential]=(words.adsr2>>5)&1;
  p[kParamReverbPreset]=static_cast<std::uint32_t>(state.reverb.preset);
  p[kParamReverbEnabled]=state.reverb.enabled?1:0;
  p[kParamReverbToneSend]=state.reverb.useToneSend?1:0;
  p[kParamReverbLeft]=state.reverb.volumeLeft*100.;
  p[kParamReverbRight]=state.reverb.volumeRight*100.;
  return p;
}
}

bool TetoricaSPU::SerializeState(iplug::IByteChunk& chunk) const
{
#if IPLUG_DSP
  std::lock_guard<std::mutex> lock(mStateMutex);
  try
  {
    // Capture regular parameters once through the framework API, then encode
    // them with the immutable authoring definition. No live SPU reads occur.
    iplug::IByteChunk params;
    if (!SerializeParams(params) || params.Size() != kNumParams*sizeof(double))
      return false;
    Parameters values{};int position=0;
    for(auto& value:values) {
      const int next=params.Get(&value,position);
      if(next!=position+sizeof(double))return false;
      position=next;
    }
    const auto definition = mEngines.snapshotDefinition();
    const auto bytes = tetorica::encodeState(definition, values[kParamGain], envelope(values), reverb(values));
    if (chunk.Size() < 0 || bytes.size() > static_cast<std::size_t>(
          std::numeric_limits<int>::max() - chunk.Size()))
      return false;
    return chunk.PutBytes(bytes.data(), static_cast<int>(bytes.size())) > 0;
  }
  catch (...)
  {
    return false;
  }
#else
  return false;
#endif
}

int TetoricaSPU::UnserializeState(const iplug::IByteChunk& chunk, int startPos)
{
#if IPLUG_DSP
  std::lock_guard<std::mutex> lock(mStateMutex);
  if (startPos < 0 || startPos > chunk.Size())
    return 0;
  const auto remaining = static_cast<std::size_t>(chunk.Size() - startPos);
  if (!remaining || remaining > tetorica::kMaxStateBytes)
    return 0;
  try
  {
    const auto* data = chunk.GetData() + startPos;
    tetorica::SavedInstrumentState state;
    if (remaining == sizeof(double))
    {
      // Old AU state was one native double; VST3 adapters strip its bypass
      // trailer. Reserve the exact new-format magic so a truncated new state
      // cannot masquerade as a very small, otherwise valid legacy Gain.
      if (std::memcmp(data, tetorica::kStateMagic.data(), sizeof(double)) == 0)
        return 0;
      std::memcpy(&state.gainPercent, data, sizeof(double));
      if (!std::isfinite(state.gainPercent) || state.gainPercent < 0. || state.gainPercent > 100.)
        return 0;
      state.definition = tetorica::InstrumentDefinition::builtin();
    }
    else
      state = tetorica::decodeState({data, remaining});

    // Complete bounded parsing/cache preparation and all chunk allocation
    // before changing request generation, pending engine or Gain.
    auto prepared = tetorica::prepareDefinition(*state.definition);
    iplug::IByteChunk params;
    for(const double value:stateParameters(state))
      if(params.Put(&value)<=0)return 0;
    if(params.Size()!=kNumParams*sizeof(double))return 0;
    mEngines.commitPrepared(std::move(prepared), state.definition);
    // Successful host restore is a quiescent lifecycle operation. Wheel and
    // pedal are live input, not preset data; a UI-only bank swap retains them.
    // Do this after the transactional commit so invalid state changes nothing.
    resetControllerCache();
    discardCarriedMidi();
    mMidiOverflow = false;
    // This trusted complete parameter chunk was fully validated and allocated
    // above. The framework sets all 17 values and calls OnParamReset.
    UnserializeParams(params, 0);
    // A host restore can arrive off the UI thread. Only publish a revision
    // here; OnIdle will refresh its model without requesting another engine.
    // Failed validation/preparation above must leave an in-flight UI load alone.
    scheduleInstrumentUIRefresh();
    return chunk.Size();
  }
  catch (...)
  {
    return 0;
  }
#else
  return 0;
#endif
}
