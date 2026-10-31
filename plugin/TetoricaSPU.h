// iPlug2 wrapper adapted from iPlug2OOS's TemplateProject and the iPlug2
// IPlugInstrument example. See UPSTREAM.md and vendor/iPlug2OOS/iPlug2/LICENSE.txt.
#pragma once

#include "IPlug_include_in_plug_hdr.h"

#if IPLUG_DSP
#include "SpuEngineExchange.h"
#include "SpuReverb.h"
#include "VabLoader.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#endif

enum EParams
{
  kParamGain = 0,
  kParamEnvelopeEnabled,
  kParamAttackRate,
  kParamAttackExponential,
  kParamDecayRate,
  kParamSustainLevel,
  kParamSustainRate,
  kParamSustainDecrease,
  kParamSustainExponential,
  kParamReleaseRate,
  kParamReleaseExponential,
  kParamEnvelopeLive,
  kParamReverbPreset,
  kParamReverbEnabled,
  kParamReverbLeft,
  kParamReverbRight,
  kParamReverbToneSend,
  kNumParams
};

enum ECtrlTags
{
  kCtrlKeyboard = 0,
  kCtrlLoadVab,
  kCtrlBuiltin,
  kCtrlBankLabel,
  kCtrlProgram,
  kCtrlTone,
  kCtrlInstrumentStatus,
  kCtrlEnvelopeGraph,
  kCtrlEnvelopeStatus,
  kCtrlEnvelopeFirst,
  kCtrlEnvelopeLast = kCtrlEnvelopeFirst + 8,
  kCtrlEnvelopeEnabled,
  kCtrlEnvelopeCopy,
  kCtrlEnvelopeLive,
  kCtrlPageEnvelope,
  kCtrlPageReverb,
  kCtrlReverbHeading,
  kCtrlReverbPreset,
  kCtrlReverbEnabled,
  kCtrlReverbLeft,
  kCtrlReverbRight,
  kCtrlReverbToneSend,
  kCtrlReverbInfo,
  kCtrlReverbStatus
};

class TetoricaSPU final : public iplug::Plugin
{
public:
  explicit TetoricaSPU(const iplug::InstanceInfo& info);
  ~TetoricaSPU() override;
  bool SerializeState(iplug::IByteChunk& chunk) const override;
  int UnserializeState(const iplug::IByteChunk& chunk, int startPos) override;

#if defined(VST3_API)
  Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
  Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
  Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
#endif

#if IPLUG_EDITOR
  void OnIdle() override;
  void OnUIOpen() override;
  void OnUIClose() override;
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
  // Host restore calls this only after a successful transactional commit.
  // UI controls and file-dialog callbacks are refreshed on the UI timer.
  void scheduleInstrumentUIRefresh() noexcept
  {
#if IPLUG_EDITOR
    mInstrumentUIRevision.fetch_add(1, std::memory_order_release);
#endif
  }
#if IPLUG_EDITOR
  struct InstrumentUIState;
  void initializeInstrumentUI();
  void closeInstrumentUI() noexcept;
  void promptForVab();
  void requestBuiltinFromUI();
  void requestProgramFromUI(
    std::shared_ptr<const tetorica::InstrumentDefinition> source,
    std::uint64_t revision, tetorica::SpuEngineExchange::Generation generation,
    std::uint32_t program);
  void requestSelectionFromUI(
    std::shared_ptr<const tetorica::InstrumentDefinition> source,
    std::uint64_t revision, tetorica::SpuEngineExchange::Generation generation,
    std::uint32_t program, std::uint32_t tone);
  void updateInstrumentControls();
  void copyToneEnvelopeFromUI();
  void showInstrumentPage(bool reverb);
  void chooseReverbPresetFromUI(int preset);
  void notifyHostOfInstrumentEdit();
  std::shared_ptr<InstrumentUIState> mInstrumentUI;
  std::atomic<std::uint64_t> mInstrumentUIRevision{0};
#endif
  void applyMidi(const iplug::IMidiMsg& msg);
  void resetControllerCache() noexcept;
  void applyControllerCache() noexcept;
  void applyEnvelopeParameters() noexcept;
  void applyReverbParameters() noexcept;
  void discardCarriedMidi() noexcept;
  void renderRange(iplug::sample** outputs, int offset, int nFrames, double gain);

  // All MIDI and render storage is allocated with the instance, never while
  // processing audio. Host offsets are kept across short/variable blocks.
  static constexpr std::size_t kMidiCapacity = 1024;
  static constexpr int kScratchFrames = 256;
  tetorica::SpuEngineExchange mEngines;
  // Host state operations are non-RT. Audio never takes this mutex or reads
  // the immutable authoring snapshot used by serialization.
  mutable std::mutex mStateMutex;
  // Destruction joins the non-RT worker before destroying the exchange.
  tetorica::VabLoader mLoader{mEngines};
  struct QueuedMidi
  {
    iplug::IMidiMsg message;
    bool carried = false;
  };
  std::array<QueuedMidi, kMidiCapacity> mMidi{};
  std::size_t mMidiCount = 0;
  bool mMidiOverflow = false;
  bool mReady = false;
  // Audio-thread-owned live MIDI state, separate from saved instrument state.
  // A UI bank/tone replacement inherits it; host restore/reset starts neutral.
  // Bend is stored as the signed displacement from raw MIDI center 8192.
  std::array<std::int16_t, 16> mBend{};
  std::array<bool, 16> mSustain{};
  tetorica::EnvelopeRegisters mAppliedEnvelope{};
  bool mEnvelopeApplied = false;
  bool mAppliedEnvelopeLive = true;
  std::array<float, kScratchFrames> mLeft{};
  std::array<float, kScratchFrames> mRight{};
#endif
};
