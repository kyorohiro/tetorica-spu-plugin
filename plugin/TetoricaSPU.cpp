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
  mMidiCount = 0;
  mMidiOverflow = false;
  mReady = mEngines.instrument().prepare(GetSampleRate());
  mEngines.instrument().panic();
}

void TetoricaSPU::OnActivate(bool active)
{
  if (!active)
  {
    mMidiCount = 0;
    mMidiOverflow = false;
    mEngines.instrument().panic();
  }
}

void TetoricaSPU::ProcessMidiMsg(const iplug::IMidiMsg& msg)
{
  using iplug::IMidiMsg;
  const auto status = msg.StatusMsg();
  if (status != IMidiMsg::kNoteOn && status != IMidiMsg::kNoteOff &&
      status != IMidiMsg::kControlChange)
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
      if (msg.mData1 == 120)
        mEngines.instrument().panic();
      else if (msg.mData1 == 123)
        mEngines.instrument().allNotesOff(msg.Channel());
      break;
    default:
      break;
  }
}

void TetoricaSPU::renderRange(iplug::sample** outputs, int offset, int nFrames, double gain)
{
  while (nFrames > 0)
  {
    const int count = std::min(nFrames, kScratchFrames);
    mEngines.instrument().render(mLeft.data(), mRight.data(), count);
    for (int frame = 0; frame < count; ++frame)
    {
      outputs[0][offset + frame] = static_cast<iplug::sample>(mLeft[frame] * gain);
      outputs[1][offset + frame] = static_cast<iplug::sample>(mRight[frame] * gain);
    }
    offset += count;
    nFrames -= count;
  }
}

void TetoricaSPU::ProcessBlock(iplug::sample**, iplug::sample** outputs, int nFrames)
{
  if (mMidiOverflow)
  {
    mEngines.instrument().panic();
    mMidiCount = 0;
    mMidiOverflow = false;
  }
  if (nFrames <= 0)
    return;
  if (mEngines.beginBlock(GetSampleRate()))
  {
    // A replacement starts with no held notes. Discard events deferred by the
    // previous instrument, while preserving new events already delivered by
    // the host for this block (including NoteOn at zero after a state restore).
    std::size_t kept = 0;
    for (std::size_t event = 0; event < mMidiCount; ++event)
      if (!mMidi[event].carried)
        mMidi[kept++] = mMidi[event];
    mMidiCount = kept;
    mReady = true;
  }
  if (!mReady)
  {
    std::fill_n(outputs[0], nFrames, iplug::sample{});
    std::fill_n(outputs[1], nFrames, iplug::sample{});
    mMidiCount = 0;
    return;
  }

  const double requestedGain = GetParam(kParamGain)->Value() / 100.;
  const double gain = std::isfinite(requestedGain) ? std::clamp(requestedGain, 0., 1.) : 0.;
  int offset = 0;
  std::size_t event = 0;
  while (event < mMidiCount && mMidi[event].message.mOffset < nFrames)
  {
    const int nextOffset = mMidi[event].message.mOffset;
    renderRange(outputs, offset, nextOffset - offset, gain);
    offset = nextOffset;
    do
    {
      applyMidi(mMidi[event++].message);
    } while (event < mMidiCount && mMidi[event].message.mOffset == nextOffset);
  }
  renderRange(outputs, offset, nFrames - offset, gain);

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
