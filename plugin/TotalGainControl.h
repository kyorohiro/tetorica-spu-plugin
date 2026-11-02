// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

// One editor gesture controls the legacy Gain and its separate boost without
// changing either host parameter's normalized mapping. The owner supplies the
// parameter gestures and refreshes this view from both host values.
class SpuTotalGainControl final : public iplug::igraphics::IControl
{
public:
  using Gesture = std::function<void()>;
  using Change = std::function<void(double)>;
  using Epoch = std::function<std::uint64_t()>;

  SpuTotalGainControl(const iplug::igraphics::IRECT& bounds, Gesture begin,
                      Change change, Gesture end, Epoch epoch)
  : IControl(bounds), mBegin(std::move(begin)), mChange(std::move(change)),
    mEnd(std::move(end)), mEpoch(std::move(epoch))
  {
    mDblAsSingleClick = true;
    SetTooltip("Master Gain 0-380%. Blue through 100%; orange above 100%. Drag the bar or use the mouse wheel. The DAW keeps the original 0-100% Gain parameter and a separate Gain boost multiplier, preserving existing automation.");
  }

  void SetGain(double percent)
  {
    if (!std::isfinite(percent)) return;
    percent = std::clamp(percent, 0., 380.);
    if (percent == mPercent) return;
    mPercent = percent;
    SetDirty(false);
  }

  void Draw(iplug::igraphics::IGraphics& graphics) override
  {
    using namespace iplug::igraphics;
    const IColor text(255, 27, 62, 73), rail(255, 199, 222, 228),
      border(255, 83, 133, 146), blue(255, 14, 144, 163), orange(255, 231, 124, 44);
    graphics.DrawText(IText(14, text), "Gain", IRECT(mRECT.L, mRECT.T + 6, mRECT.R, mRECT.T + 26), &mBlend);
    char label[32];
    std::snprintf(label, sizeof(label), "%.1f%%", mPercent);
    graphics.DrawText(IText(22, mPercent > 100. ? orange : text), label,
      IRECT(mRECT.L, mRECT.T + 30, mRECT.R, mRECT.T + 58), &mBlend);
    const auto track = trackBounds();
    graphics.FillRect(rail, track, &mBlend);
    graphics.FillRect(mPercent > 100. ? orange : blue,
      IRECT(track.L, track.T, track.L + track.W() * float(mPercent / 380.), track.B), &mBlend);
    graphics.DrawRect(border, track, &mBlend);
    const auto normal = track.L + track.W() * 100.f / 380.f;
    graphics.DrawLine(border, normal, track.T - 3, normal, track.B + 3, &mBlend);
    graphics.DrawText(IText(11, text), "0     100        380%",
      IRECT(mRECT.L, track.B + 5, mRECT.R, mRECT.B), &mBlend);
  }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    if (!mod.L || IsDisabled() || IsHidden() || !std::isfinite(x) ||
        !std::isfinite(y) || !mRECT.Contains(x, y)) return;
    CancelPointerEdit();
    mGestureEpoch = mEpoch();
    mActive = true;
    if (mBegin) mBegin();
    move(x);
  }
  void OnMouseDrag(float x, float, float, float, const iplug::igraphics::IMouseMod&) override
  {
    if (!mActive) return;
    if (IsDisabled() || IsHidden() || mGestureEpoch != mEpoch())
    { CancelPointerEdit(); return; }
    move(x);
  }
  void OnMouseUp(float, float, const iplug::igraphics::IMouseMod&) override { CancelPointerEdit(); }
  void OnTouchCancelled(float, float, const iplug::igraphics::IMouseMod&) override { CancelPointerEdit(); }
  void OnMouseOut() override
  {
    CancelPointerEdit();
    IControl::OnMouseOut();
  }
  void OnMouseWheel(float, float, const iplug::igraphics::IMouseMod& mod, float delta) override
  {
    if (mActive || IsDisabled() || IsHidden() || !std::isfinite(delta) || !delta) return;
    mGestureEpoch = mEpoch();
    mActive = true;
    if (mBegin) mBegin();
    send(std::clamp(mPercent + double(delta) * (mod.S ? .1 : 1.), 0., 380.));
    CancelPointerEdit();
  }

  void CancelPointerEdit()
  {
    if (!mActive) return;
    mActive = false;
    // Ending a host gesture writes no parameter values, including after a
    // restore. Both Begin calls must always receive their matching End.
    if (mEnd) mEnd();
  }
  void CancelStalePointerEdit()
  {
    if (mActive && mGestureEpoch != mEpoch()) CancelPointerEdit();
  }

private:
  iplug::igraphics::IRECT trackBounds() const
  { return {mRECT.L + 8, mRECT.T + 69, mRECT.R - 8, mRECT.T + 87}; }
  void move(float x)
  {
    if (!std::isfinite(x)) return;
    const auto track = trackBounds();
    send(std::round(std::clamp((double(x) - track.L) / track.W(), 0., 1.) * 3800.) / 10.);
  }
  void send(double value)
  {
    if (mGestureEpoch != mEpoch()) { CancelPointerEdit(); return; }
    if (mChange && value != mPercent) mChange(value);
    SetDirty(false);
  }

  Gesture mBegin;
  Change mChange;
  Gesture mEnd;
  Epoch mEpoch;
  std::uint64_t mGestureEpoch = 0;
  bool mActive = false;
  double mPercent = 100.;
};
