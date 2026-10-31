// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "TetoricaSPU.h"
#include "InstrumentState.h"
#include <cmath>
#include <cstring>
#include <limits>

bool TetoricaSPU::SerializeState(iplug::IByteChunk& chunk) const
{
#if IPLUG_DSP
  std::lock_guard<std::mutex> lock(mStateMutex);
  try
  {
    // Capture regular parameters once through the framework API, then encode
    // them with the immutable authoring definition. No live SPU reads occur.
    iplug::IByteChunk params;
    if (!SerializeParams(params) || params.Size() != sizeof(double))
      return false;
    double gain = 0.;
    if (params.Get(&gain, 0) != sizeof(double))
      return false;
    const auto definition = mEngines.snapshotDefinition();
    const auto bytes = tetorica::encodeState(definition, gain);
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
    if (params.Put(&state.gainPercent) != sizeof(double))
      return 0;
    mEngines.commitPrepared(std::move(prepared), state.definition);
    // This trusted one-double chunk was fully validated above. The pinned
    // framework then sets Gain and calls OnParamReset without further reads.
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
