// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "TetoricaSPU.h"

#if defined(VST3_API)
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include <array>
#include <algorithm>
#include <cmath>

namespace {
// A borrowed, synchronous view. The pinned base process only enumerates/reads
// these host-owned queues; it neither retains this stack object nor adds data.
class RegularParameters final : public Steinberg::Vst::IParameterChanges
{
public:
  void include(Steinberg::Vst::IParamValueQueue* queue)
  {
    for (std::size_t i=0;i<mCount;++i)
      if(mQueues[i]->getParameterId()==queue->getParameterId())return;
    if(mCount<mQueues.size())mQueues[mCount++]=queue;
  }
  Steinberg::int32 PLUGIN_API getParameterCount() override
  { return static_cast<Steinberg::int32>(mCount); }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API getParameterData(Steinberg::int32 index) override
  { return index>=0&&static_cast<std::size_t>(index)<mCount?mQueues[index]:nullptr; }
  Steinberg::Vst::IParamValueQueue* PLUGIN_API addParameterData(
    const Steinberg::Vst::ParamID&, Steinberg::int32& index) override
  { index=-1;return nullptr; }
  Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid,void** object) override
  {
    if(!object)return Steinberg::kInvalidArgument;
    *object=nullptr;
    if(Steinberg::FUnknownPrivate::iidEqual(iid,Steinberg::Vst::IParameterChanges::iid)||
       Steinberg::FUnknownPrivate::iidEqual(iid,Steinberg::FUnknown::iid))
    { *object=static_cast<Steinberg::Vst::IParameterChanges*>(this);return Steinberg::kResultOk; }
    return Steinberg::kNoInterface;
  }
  // No heap ownership is transferred by this temporary ProcessData view.
  Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
  Steinberg::uint32 PLUGIN_API release() override { return 1; }
private:
  std::array<Steinberg::Vst::IParamValueQueue*,kNumParams+1> mQueues{};
  std::size_t mCount=0;
};
}

Steinberg::tresult PLUGIN_API TetoricaSPU::process(Steinberg::Vst::ProcessData& data)
{
  using namespace Steinberg;
  using namespace Steinberg::Vst;
  if(data.numSamples<0)return kInvalidArgument;
  RegularParameters regular;
  std::size_t inspected=0;
  constexpr int32 maximumQueues=16*kCountCtrlNumber+kNumParams+1;
  if(auto* changes=data.inputParameterChanges)
  {
    const int32 count=changes->getParameterCount();
    if(count>maximumQueues){mMidiOverflow=true;mMidiCount=0;}
    for(int32 index=0;index<count&&index<maximumQueues;++index)
    {
      auto* queue=changes->getParameterData(index);
      if(!queue)continue;
      const auto id=queue->getParameterId();
      if(id<static_cast<ParamID>(kNumParams)||id==static_cast<ParamID>(iplug::kBypassParam))
      { regular.include(queue);continue; }
      if(id<static_cast<ParamID>(iplug::kMIDICCParamStartIdx))continue;
      const auto mapped=id-static_cast<ParamID>(iplug::kMIDICCParamStartIdx);
      if(mapped>=16*kCountCtrlNumber)continue;
      const auto channel=mapped/kCountCtrlNumber,controller=mapped%kCountCtrlNumber;
      if(controller!=kPitchBend&&controller!=64&&controller!=120&&controller!=121&&controller!=123)continue;
      const int32 points=queue->getPointCount();
      if(points<0)continue;
      if(static_cast<std::size_t>(points)>kMidiCapacity-inspected)
      { mMidiOverflow=true;mMidiCount=0;inspected=kMidiCapacity;continue; }
      if(mMidiOverflow)continue;
      for(int32 point=0;point<points;++point)
      {
        ++inspected;
        int32 offset=0;ParamValue value=0;
        if(queue->getPoint(point,offset,value)!=kResultOk||offset<0||
           !std::isfinite(value)||value<0||value>1)continue;
        if(controller==kPitchBend)
        {
          const int raw=std::min(16383,static_cast<int>(value*16384.));
          ProcessMidiMsg(iplug::IMidiMsg(offset,0xe0|channel,raw&127,raw>>7));
        }
        else ProcessMidiMsg(iplug::IMidiMsg(offset,0xb0|channel,controller,
          static_cast<int>(std::lround(value*127.))));
        if(mMidiOverflow)break;
      }
    }
  }
  // All mapped queues (including unsupported controllers) are removed so the
  // pinned base's last-point-only CC conversion cannot duplicate these events.
  // Its ordinary Gain/bypass and note-event handling remain intact.
  auto forwarded=data;
  forwarded.inputParameterChanges=&regular;
  const auto result=iplug::Plugin::process(forwarded);
  if(GetBypassed())
  {
    // The pinned base skips ProcessBlock while bypassed, but still delivers
    // incoming MIDI. Those offsets belong to this discarded block and cannot
    // be replayed/sorted against later blocks when processing resumes.
    mMidiCount=0;
    mMidiOverflow=false;
    resetControllerCache();
    mEngines.instrument().panic();
    mEngines.instrument().resetControllers();
    mGainNeedsReset=true;
  }
  return result;
}
#endif
