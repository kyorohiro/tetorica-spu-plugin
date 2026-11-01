// Native SPU integration and bounded MIDI scheduling by Tetorica contributors.
// The iPlug2 UI/bootstrap follows the upstream examples listed in UPSTREAM.md.
#include "TetoricaSPU.h"
#include "IPlug_include_in_plug_src.h"

#include <algorithm>
#include <cmath>

TetoricaSPU::TetoricaSPU(const iplug::InstanceInfo& info)
: iplug::Plugin(info, iplug::MakeConfig(kNumParams, 1))
{
  GetParam(kParamGain)->InitDouble("Gain", 100., 0., 100., 0.1, "%");
  GetParam(kParamEnvelopeEnabled)->InitBool("Custom ADSR", false);
  GetParam(kParamAttackRate)->InitInt("Attack rate", 0, 0, 127);
  GetParam(kParamAttackExponential)->InitBool("Attack exponential", false);
  GetParam(kParamDecayRate)->InitInt("Decay rate", 0, 0, 15);
  GetParam(kParamSustainLevel)->InitInt("Sustain level", 15, 0, 15);
  GetParam(kParamSustainRate)->InitInt("Sustain rate", 127, 0, 127);
  GetParam(kParamSustainDecrease)->InitBool("Sustain decrease", false);
  GetParam(kParamSustainExponential)->InitBool("Sustain exponential", false);
  GetParam(kParamReleaseRate)->InitInt("Release rate", 0, 0, 31);
  GetParam(kParamReleaseExponential)->InitBool("Release exponential", false);
  GetParam(kParamEnvelopeLive)->InitBool("Apply ADSR while playing", true);
  GetParam(kParamReverbPreset)->InitEnum("Reverb preset", 0,
    {"Off", "Room", "Studio Small", "Studio Medium", "Studio Large", "Hall", "Space Echo", "Half Echo"});
  GetParam(kParamReverbEnabled)->InitBool("Enable reverb", false);
  GetParam(kParamReverbLeft)->InitDouble("Reverb return left", 25., 0., 100., 0.1, "%");
  GetParam(kParamReverbRight)->InitDouble("Reverb return right", 25., 0., 100., 0.1, "%");
  GetParam(kParamReverbToneSend)->InitBool("Use Tone reverb send", false);
  GetParam(kParamNoiseEnabled)->InitBool("SPU Noise", false);
  GetParam(kParamNoiseClock)->InitInt("Shared Noise clock", 48, 0, 63);
  GetParam(kParamPmonEnabled)->InitBool("SPU PMON (Noise takes priority)", false);
  GetParam(kParamPmonWaveform)->InitEnum("PMON modulator", 0,
    {"Sine", "Triangle", "Saw", "Square"});
  GetParam(kParamPmonRatio)->InitDouble("PMON ratio", 1., 0.01, 16., 0.01);
  GetParam(kParamPmonDepth)->InitDouble("PMON depth", 0.5, 0., 1., 0.001);
  GetParam(kParamPmonDecay)->InitDouble("PMON decay (0 = Hold)", 500., 0., 5000., 1., "ms");
  GetParam(kParamMorphEnabled)->InitBool("Enable software morph", false);
  GetParam(kParamMorphAmount)->InitDouble("Software morph A to B", 0., 0., 1., 0.001);

#if IPLUG_EDITOR
  mMakeGraphicsFunc = [this]() {
    return iplug::igraphics::MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS);
  };
  initializeInstrumentUI();
#endif
}

TetoricaSPU::~TetoricaSPU()
{
#if IPLUG_EDITOR
  closeInstrumentUI();
#endif
}

#if IPLUG_DSP
void TetoricaSPU::OnReset()
{
  mUiMidi.discardPending();
  mMidiCount = 0;
  mMidiOverflow = false;
  resetControllerCache();
  mReady = mEngines.instrument().prepare(GetSampleRate());
  mEngines.instrument().panic();
  mEnvelopeApplied = false;
  mGainNeedsReset = true;
}

void TetoricaSPU::OnActivate(bool active)
{
  if (!active)
  {
    mUiMidi.discardPending();
    mMidiCount = 0;
    mMidiOverflow = false;
    resetControllerCache();
    mEngines.instrument().panic();
    mEngines.instrument().resetControllers();
    mGainNeedsReset = true;
  }
}

void TetoricaSPU::SendMidiMsgFromUI(const iplug::IMidiMsg& msg)
{
  mUiMidi.push({false, msg.mOffset, msg.mStatus, msg.mData1, msg.mData2});
  // Keep delegate forwarding without also entering the framework's separate
  // MIDI FIFO: that would destroy the ordering with our global-stop command.
  EDITOR_DELEGATE_CLASS::SendMidiMsgFromUI(msg);
}

void TetoricaSPU::drainUiMidi() noexcept
{
  if (mUiMidi.takeOverflow())
  {
    mUiMidi.discardPending();
    mMidiOverflow = true;
    return;
  }
  const auto count = mUiMidi.pending(); // Fixed snapshot; no unbounded UI drain.
  tetorica::UiMidiCommand command;
  for (std::size_t i = 0; i < count && mUiMidi.pop(command); ++i)
  {
    if (command.globalPanic)
    {
      mEngines.instrument().panic();
      // Notes before the stop, including undrained UI NoteOns and future host
      // notes, cannot replay. Commands later in this FIFO may play normally.
      discardQueuedNotes(false);
    }
    else
      ProcessMidiMsg(iplug::IMidiMsg(command.offset, command.status, command.data1, command.data2));
  }
}

void TetoricaSPU::ProcessMidiMsg(const iplug::IMidiMsg& msg)
{
  using iplug::IMidiMsg;
  const auto status = msg.StatusMsg();
  if (status != IMidiMsg::kNoteOn && status != IMidiMsg::kNoteOff &&
      status != IMidiMsg::kControlChange && status != IMidiMsg::kPitchWheel)
    return;
  // Raw MIDI data bytes have seven bits. Do not let malformed input wrap to
  // a different wheel position or controller value.
  if (msg.mData1 > 127 || msg.mData2 > 127)
    return;
  if (mMidiOverflow)
    return;
  if (mMidiCount == kMidiCapacity)
  {
    // A dropped NoteOff must never strand a voice. Stop all sound at the next
    // render boundary and discard this batch, including any later NoteOns.
    mMidiOverflow = true;
    mMidiCount = 0;
    return;
  }

  auto queued = msg;
  queued.mOffset = std::max(0, queued.mOffset);
  auto position = mMidiCount++;
  // Stable insertion keeps same-sample NoteOff/NoteOn ordering from the host.
  while (position > 0 && mMidi[position - 1].message.mOffset > queued.mOffset)
  {
    mMidi[position] = mMidi[position - 1];
    --position;
  }
  mMidi[position] = {queued, false};
}

void TetoricaSPU::applyMidi(const iplug::IMidiMsg& msg)
{
  using iplug::IMidiMsg;
  const auto channel = static_cast<std::size_t>(msg.Channel());
  switch (msg.StatusMsg())
  {
    case IMidiMsg::kNoteOn:
      if (msg.Velocity() > 0)
        mEngines.instrument().noteOn(msg.NoteNumber(), msg.Velocity(), msg.Channel());
      else
        mEngines.instrument().noteOff(msg.NoteNumber(), msg.Channel());
      break;
    case IMidiMsg::kNoteOff:
      mEngines.instrument().noteOff(msg.NoteNumber(), msg.Channel());
      break;
    case IMidiMsg::kControlChange:
      if (msg.mData1 == 64)
      {
        mSustain[channel] = msg.mData2 >= 64;
        mEngines.instrument().setSustain(mSustain[channel], msg.Channel());
      }
      else if (msg.mData1 == 120)
        mEngines.instrument().allSoundOff(msg.Channel());
      else if (msg.mData1 == 121)
      {
        mBend[channel] = 0;
        mSustain[channel] = false;
        mEngines.instrument().resetControllers(msg.Channel());
      }
      else if (msg.mData1 == 123)
        mEngines.instrument().allNotesOff(msg.Channel());
      break;
    case IMidiMsg::kPitchWheel:
    {
      const int value = int(msg.mData1) | (int(msg.mData2) << 7);
      mBend[channel] = static_cast<std::int16_t>(value - 8192);
      mEngines.instrument().setPitchBend(value, msg.Channel());
      break;
    }
    default:
      break;
  }
}

void TetoricaSPU::resetControllerCache() noexcept
{
  mBend.fill(0);
  mSustain.fill(false);
}

void TetoricaSPU::applyControllerCache() noexcept
{
  for (int channel = 0; channel < 16; ++channel)
  {
    mEngines.instrument().setPitchBend(int(mBend[channel]) + 8192, channel);
    mEngines.instrument().setSustain(mSustain[channel], channel);
  }
}

void TetoricaSPU::applyEnvelopeParameters() noexcept
{
  // Host parameters are read at block boundaries. Named editing changes only
  // interpreted bits; the selected Tone's uninterpreted ADSR2 bit13 survives.
  auto& instrument = mEngines.instrument();
  const auto source = instrument.sourceEnvelope();
  auto envelope = source;
  const auto value = [this](int index, int maximum) noexcept {
    const double raw = GetParam(index)->Value();
    return std::isfinite(raw) ? static_cast<unsigned>(
      std::clamp(std::round(raw), 0., static_cast<double>(maximum))) : 0u;
  };
  if (value(kParamEnvelopeEnabled, 1))
  {
    envelope.adsr1 = static_cast<std::uint16_t>(
      (value(kParamAttackExponential, 1) << 15) |
      (value(kParamAttackRate, 127) << 8) |
      (value(kParamDecayRate, 15) << 4) | value(kParamSustainLevel, 15));
    envelope.adsr2 = static_cast<std::uint16_t>((source.adsr2 & 0x2000) |
      (value(kParamSustainExponential, 1) << 15) |
      (value(kParamSustainDecrease, 1) << 14) |
      (value(kParamSustainRate, 127) << 6) |
      (value(kParamReleaseExponential, 1) << 5) | value(kParamReleaseRate, 31));
  }
  const bool live = value(kParamEnvelopeLive, 1) != 0;
  if (!mEnvelopeApplied || envelope.adsr1 != mAppliedEnvelope.adsr1 ||
      envelope.adsr2 != mAppliedEnvelope.adsr2 || (live && !mAppliedEnvelopeLive))
    instrument.setEnvelope(envelope, live);
  mAppliedEnvelope = envelope;
  mAppliedEnvelopeLive = live;
  mEnvelopeApplied = true;
}

void TetoricaSPU::discardCarriedMidi() noexcept
{
  std::size_t kept = 0;
  for (std::size_t event = 0; event < mMidiCount; ++event)
    if (!mMidi[event].carried)
      mMidi[kept++] = mMidi[event];
  mMidiCount = kept;
}

void TetoricaSPU::discardQueuedNotes(bool carriedOnly) noexcept
{
  std::size_t kept = 0;
  for (std::size_t event = 0; event < mMidiCount; ++event)
    if ((carriedOnly && !mMidi[event].carried) ||
        mMidi[event].message.StatusMsg() == iplug::IMidiMsg::kControlChange ||
        mMidi[event].message.StatusMsg() == iplug::IMidiMsg::kPitchWheel)
      mMidi[kept++] = mMidi[event];
  mMidiCount = kept;
}

void TetoricaSPU::applySourceParameters() noexcept
{
  const double enabled = GetParam(kParamNoiseEnabled)->Value();
  const double clock = GetParam(kParamNoiseClock)->Value();
  const double pmonEnabled = GetParam(kParamPmonEnabled)->Value();
  const double waveform = GetParam(kParamPmonWaveform)->Value();
  const double ratio = GetParam(kParamPmonRatio)->Value();
  const double depth = GetParam(kParamPmonDepth)->Value();
  const double decay = GetParam(kParamPmonDecay)->Value();
  const double morphEnabled = GetParam(kParamMorphEnabled)->Value();
  const double morphAmount = GetParam(kParamMorphAmount)->Value();
  if (!std::isfinite(enabled) || enabled < 0 || enabled > 1 || std::floor(enabled) != enabled ||
      !std::isfinite(clock) || clock < 0 || clock > 63 || std::floor(clock) != clock ||
      !std::isfinite(pmonEnabled) || pmonEnabled < 0 || pmonEnabled > 1 || std::floor(pmonEnabled) != pmonEnabled ||
      !std::isfinite(waveform) || waveform < 0 || waveform > 3 || std::floor(waveform) != waveform ||
      !std::isfinite(ratio) || ratio < 0.01 || ratio > 16 ||
      !std::isfinite(depth) || depth < 0 || depth > 1 ||
      !std::isfinite(decay) || decay < 0 || decay > 5000 ||
      !std::isfinite(morphEnabled) || morphEnabled < 0 || morphEnabled > 1 || std::floor(morphEnabled) != morphEnabled ||
      !std::isfinite(morphAmount) || morphAmount < 0 || morphAmount > 1)
    return;
  auto& instrument = mEngines.instrument();
  const bool previousNoise = instrument.noise().enabled;
  const bool previousPmon = instrument.pmon().enabled;
  const bool previousMorph = instrument.effectiveMorphEnabled();
  const tetorica::NoiseSettings settings{enabled != 0, static_cast<std::uint32_t>(clock)};
  const tetorica::PmonSettings pmon{pmonEnabled != 0 && !settings.enabled,
    static_cast<tetorica::WaveformShape>(static_cast<unsigned>(waveform)), ratio, depth, decay};
  // Raw host flags are independently automatable and saved verbatim. Resolve
  // one mode before applying: a simultaneous Noise+PMON tuple uses Noise.
  // This avoids toggling/panicking between both modes on every audio block.
  instrument.setNoise(settings);
  instrument.setPmon(pmon);
  // A/B are immutable source authoring data, prepared on the worker. Only the
  // blend amount changes their private carrier cache on this audio thread.
  // Keep raw Morph enabled while Noise hides it, so returning restores it.
  instrument.setMorph({morphEnabled != 0, morphAmount});
  if (previousNoise != instrument.noise().enabled || previousPmon != instrument.pmon().enabled ||
      previousMorph != instrument.effectiveMorphEnabled())
    // The old source's future NoteOns must not restart after the mode panic.
    // Current-block host events and carried wheel/pedal still belong to input.
    discardQueuedNotes(true);
}

void TetoricaSPU::applyReverbParameters() noexcept
{
  // These are independent saved parameters. Only a manual preset selection
  // enables the UI checkbox; host automation and restore keep its exact value.
  const double preset = GetParam(kParamReverbPreset)->Value();
  const double enabled = GetParam(kParamReverbEnabled)->Value();
  const double toneSend = GetParam(kParamReverbToneSend)->Value();
  if (!std::isfinite(preset) || preset < 0 || preset > 7 || std::floor(preset) != preset ||
      !std::isfinite(enabled) || enabled < 0 || enabled > 1 || std::floor(enabled) != enabled ||
      !std::isfinite(toneSend) || toneSend < 0 || toneSend > 1 || std::floor(toneSend) != toneSend)
    return;
  tetorica::ReverbSettings settings;
  settings.preset = static_cast<tetorica::ReverbPreset>(static_cast<unsigned>(preset));
  settings.enabled = enabled != 0;
  settings.useToneSend = toneSend != 0;
  settings.volumeLeft = GetParam(kParamReverbLeft)->Value() / 100.;
  settings.volumeRight = GetParam(kParamReverbRight)->Value() / 100.;
  mEngines.instrument().setReverb(settings);
}

void TetoricaSPU::renderRange(iplug::sample** outputs, int offset, int nFrames)
{
  while (nFrames > 0)
  {
    const int count = std::min(nFrames, kScratchFrames);
    mEngines.instrument().render(mLeft.data(), mRight.data(), count);
    for (int frame = 0; frame < count; ++frame)
    {
      const double gain = mGain.next(); // One shared coefficient per stereo frame.
      outputs[0][offset + frame] = static_cast<iplug::sample>(mLeft[frame] * gain);
      outputs[1][offset + frame] = static_cast<iplug::sample>(mRight[frame] * gain);
    }
    offset += count;
    nFrames -= count;
  }
}

void TetoricaSPU::recoverMidiOverflow() noexcept
{
  if (mMidiOverflow)
  {
    mEngines.instrument().panic();
    // Part of a pedal/wheel sequence may have been dropped along with a
    // NoteOff. Return both the engine and its swap cache to a known baseline.
    mEngines.instrument().resetControllers();
    resetControllerCache();
    mMidiCount = 0;
    mMidiOverflow = false;
  }
}

void TetoricaSPU::ProcessBlock(iplug::sample**, iplug::sample** outputs, int nFrames)
{
  recoverMidiOverflow();
  if (nFrames <= 0)
    return;
  if (mEngines.beginBlock(GetSampleRate()))
  {
    if(const auto* imported=mEngines.adoptedImportState())
    {
      applyImportedParameters(*imported);
      mGainNeedsReset = true;
      mImportAppliedGeneration.store(mEngines.adoptedGeneration(),std::memory_order_release);
    }
    applyControllerCache();
    mEnvelopeApplied = false;
    // A UI replacement starts with no held notes, but physical wheel/pedal
    // input still belongs to the MIDI stream. Preserve carried controllers,
    // especially a pedal-up at the block boundary, and discard old note events.
    // Successful host restore has already discarded all carried MIDI. Fresh
    // events for this block (including first post-restore NoteOn) stay ordered.
    discardQueuedNotes(true);
    mReady = true;
  }
  drainUiMidi();
  recoverMidiOverflow();
  if (!mReady)
  {
    std::fill_n(outputs[0], nFrames, iplug::sample{});
    std::fill_n(outputs[1], nFrames, iplug::sample{});
    mMidiCount = 0;
    return;
  }

  applyEnvelopeParameters();
  applyReverbParameters();
  applySourceParameters();

  const double requestedGain = GetParam(kParamGain)->Value() / 100.;
  // Ordinary host automation supplies one target per block. The ramp follows
  // output sample time, across scratch chunks, MIDI offsets and later blocks.
  if (mGainNeedsReset)
  {
    mGain.reset(GetSampleRate(), requestedGain);
    mGainNeedsReset = false;
  }
  else
    mGain.setTarget(requestedGain);
  int offset = 0;
  std::size_t event = 0;
  while (event < mMidiCount && mMidi[event].message.mOffset < nFrames)
  {
    const int nextOffset = mMidi[event].message.mOffset;
    renderRange(outputs, offset, nextOffset - offset);
    offset = nextOffset;
    do
    {
      applyMidi(mMidi[event++].message);
    } while (event < mMidiCount && mMidi[event].message.mOffset == nextOffset);
  }
  renderRange(outputs, offset, nFrames - offset);

  // Preserve a host event exactly at the end of the block for sample zero of
  // the next block, as well as any later event supplied by a host.
  for (std::size_t remaining = event; remaining < mMidiCount; ++remaining)
  {
    auto msg = mMidi[remaining];
    msg.message.mOffset -= nFrames;
    msg.carried = true;
    mMidi[remaining - event] = msg;
  }
  mMidiCount -= event;
}
#endif
