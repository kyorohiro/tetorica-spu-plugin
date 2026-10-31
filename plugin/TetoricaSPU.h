// iPlug2 wrapper adapted from iPlug2OOS's TemplateProject and the iPlug2
// IPlugInstrument example. See UPSTREAM.md and vendor/iPlug2OOS/iPlug2/LICENSE.txt.
#pragma once

#include "IPlug_include_in_plug_hdr.h"

#if IPLUG_DSP
#include "SpuEngineExchange.h"
#include <array>
#include <cstddef>
#include <mutex>
#endif

enum EParams
{
  kParamGain = 0,
  kNumParams
};

enum ECtrlTags
{
  kCtrlKeyboard = 0
};

class TetoricaSPU final : public iplug::Plugin
{
public:
  explicit TetoricaSPU(const iplug::InstanceInfo& info);
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

#if defined(VST3_API)
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
#endif
#if defined(AU_API)
  OSStatus SetState(CFPropertyListRef state) override;
  OSStatus GetState(CFPropertyListRef* state) override;
#endif

#if IPLUG_DSP
  void ProcessBlock(iplug::sample** inputs, iplug::sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const iplug::IMidiMsg& msg) override;
  void OnReset() override;
  void OnActivate(bool active) override;

private:
  void applyMidi(const iplug::IMidiMsg& msg);
  void renderRange(iplug::sample** outputs, int offset, int nFrames, double gain);

  // All MIDI and render storage is allocated with the instance, never while
  // processing audio. Host offsets are kept across short/variable blocks.
  static constexpr std::size_t kMidiCapacity = 1024;
  static constexpr int kScratchFrames = 256;
  tetorica::SpuEngineExchange mEngines;
  // Host state operations are non-RT. Audio never takes this mutex or reads
  // the immutable authoring snapshot used by serialization.
  mutable std::mutex mStateMutex;
  struct QueuedMidi
  {
    iplug::IMidiMsg message;
    bool carried = false;
  };
  std::array<QueuedMidi, kMidiCapacity> mMidi{};
  std::size_t mMidiCount = 0;
  bool mMidiOverflow = false;
  bool mReady = false;
  std::array<float, kScratchFrames> mLeft{};
  std::array<float, kScratchFrames> mRight{};
#endif
};
