// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include "KeyboardPerformanceState.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

// Momentary performance input, never a host parameter. The epoch invalidates
// old pointer events after reset/restore without sending MIDI into new sound.
class SpuPitchWheelControl final : public iplug::igraphics::IControl
{
public:
  using Send = std::function<void(int)>;
  using Epoch = std::function<std::uint64_t()>;

  SpuPitchWheelControl(const iplug::igraphics::IRECT& bounds, Send send, Epoch epoch)
  : IControl(bounds), mSend(std::move(send)), mEpoch(std::move(epoch))
  {
    mDblAsSingleClick = true;
    SetTooltip("Momentary Pitch Bend, +/-2 semitones on MIDI channel 1. Release returns to center. Noise is not tuned by Pitch Bend.");
  }

  void Draw(iplug::igraphics::IGraphics& graphics) override
  {
    using namespace iplug::igraphics;
    const IColor text(255, 27, 62, 73), rail(255, 199, 222, 228),
      border(255, 83, 133, 146), accent(255, 14, 144, 163);
    const auto track = trackBounds();
    graphics.DrawText(IText(12, text), "Pitch", IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 18), &mBlend);
    graphics.FillRect(rail, track, &mBlend);
    graphics.DrawRect(border, track, &mBlend);
    graphics.DrawLine(border, track.L - 5, track.MH(), track.R + 5, track.MH(), &mBlend);
    const double displacement = mWheel.value() - 8192.;
    const double position = displacement <= 0. ? mWheel.value() / 16384. :
      .5 + displacement / 16382.;
    const float y = track.B - float(position) * track.H();
    graphics.FillRect(accent, IRECT(track.L - 5, y - 3, track.R + 5, y + 3), &mBlend);
    char label[24];
    std::snprintf(label, sizeof(label), "%+.2f", 2. * displacement / (displacement <= 0. ? 8192. : 8191.));
    graphics.DrawText(IText(11, text), label, IRECT(mRECT.L, mRECT.B - 20, mRECT.R, mRECT.B), &mBlend);
  }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    if (!mod.L || IsDisabled() || IsHidden() || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto track = trackBounds();
    if (x < mRECT.L || x > mRECT.R || y < track.T || y > track.B) return;
    if (mWheel.active()) CancelPointerEdit();
    mGestureEpoch = mEpoch();
    emit(mWheel.begin(position(y)));
  }

  void OnMouseDrag(float, float y, float, float, const iplug::igraphics::IMouseMod&) override
  {
    if (mGestureEpoch != mEpoch()) { ResetPointerEdit(); return; }
    if (IsDisabled() || IsHidden()) { CancelPointerEdit(); return; }
    emit(mWheel.move(position(y)));
  }

  void OnMouseUp(float, float, const iplug::igraphics::IMouseMod&) override { CancelPointerEdit(); }
  void OnTouchCancelled(float, float, const iplug::igraphics::IMouseMod&) override { CancelPointerEdit(); }
  void OnMouseOut() override
  {
    CancelPointerEdit();
    IControl::OnMouseOut();
  }

  void CancelPointerEdit()
  {
    if (mGestureEpoch != mEpoch()) { ResetPointerEdit(); return; }
    emit(mWheel.end());
  }

  void ResetPointerEdit()
  {
    mWheel.reset();
    SetDirty(false);
  }

  void ResetStalePointerEdit()
  {
    // A fresh gesture may begin after restore but before the next UI timer.
    // Its eventual release must still send center; only forget old gestures.
    if (mGestureEpoch != mEpoch()) ResetPointerEdit();
  }

private:
  iplug::igraphics::IRECT trackBounds() const
  {
    return {mRECT.L + 20, mRECT.T + 24, mRECT.R - 20, mRECT.B - 24};
  }
  double position(float y) const
  {
    const auto track = trackBounds();
    return (track.B - double(y)) / track.H();
  }
  void emit(std::optional<int> value)
  {
    if (value && mSend) mSend(*value);
    SetDirty(false);
  }

  tetorica::ui::PitchWheelState mWheel;
  Send mSend;
  Epoch mEpoch;
  std::uint64_t mGestureEpoch = 0;
};
