// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

// Include after TetoricaSPU.h and IControls.h. This is an editor-side rate-code
// guide: the widths and curves do not predict elapsed SPU envelope time.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

class SpuEnvelopeGraphControl final : public iplug::igraphics::IControl
{
public:
  explicit SpuEnvelopeGraphControl(const iplug::igraphics::IRECT& bounds)
  : IControl(bounds, {kParamAttackRate, kParamDecayRate, kParamSustainLevel,
                     kParamSustainRate, kParamReleaseRate})
  {
    mDblAsSingleClick = true;
    SetTooltip("Drag A, D, S or R horizontally: left is faster. Drag D vertically "
               "to change sustain level. This is a rate-code guide, not elapsed time.");
  }

  void Draw(iplug::igraphics::IGraphics& graphics) override
  {
    using namespace iplug::igraphics;
    const auto shape = geometry();
    const IColor background(255, 22, 34, 42);
    const IColor grid(255, 51, 68, 78);
    const IColor text(255, 188, 204, 213);
    const IColor accent(255, 116, 195, 234);
    const IColor guide = shape.attackStopped ? IColor(255, 97, 124, 140) : accent;
    graphics.FillRect(background, mRECT, &mBlend);
    graphics.DrawRect(grid, mRECT, &mBlend);

    const IText caption(11, text);
    graphics.DrawText(caption.WithAlign(EAlign::Near), "Rate guide - not elapsed time",
      IRECT(mRECT.L + 10, mRECT.T + 1, mRECT.MW(), mRECT.T + 16), &mBlend);
    char state[96];
    std::snprintf(state, sizeof(state), "%s%s%s",
      shape.attackStopped ? "A stopped  " : "",
      shape.sustainHeld ? "S hold  " : "",
      shape.releaseHeld ? "R hold" : "");
    graphics.DrawText(caption.WithAlign(EAlign::Far), state,
      IRECT(mRECT.MW(), mRECT.T + 1, mRECT.R - 10, mRECT.T + 16), &mBlend);

    for (float level : {0.f, .5f, 1.f})
    {
      const float y = shape.bottom - level * (shape.bottom - shape.top);
      graphics.DrawLine(grid, shape.left, y, shape.right, y, &mBlend);
    }

    const auto& attack = shape.handles[0];
    const auto& decay = shape.handles[1];
    const auto& sustain = shape.handles[2];
    const auto& release = shape.handles[3];
    graphics.PathClear();
    graphics.PathMoveTo(shape.left, shape.bottom);
    if (shape.attackExponential)
      graphics.PathQuadraticBezierTo(shape.left + 10, shape.top + 2, attack.x, attack.y);
    else
      graphics.PathLineTo(attack.x, attack.y);
    graphics.PathQuadraticBezierTo(attack.x + 10, decay.y, decay.x, decay.y);
    if (shape.sustainExponential && !shape.sustainHeld)
      graphics.PathQuadraticBezierTo(decay.x + 10, sustain.y, sustain.x, sustain.y);
    else
      graphics.PathLineTo(sustain.x, sustain.y);
    if (shape.releaseExponential && !shape.releaseHeld)
      graphics.PathQuadraticBezierTo(sustain.x + 10, release.y, release.x, release.y);
    else
      graphics.PathLineTo(release.x, release.y);
    graphics.PathStroke(IPattern(guide), 2.f, IStrokeOptions(), &mBlend);

    // Keep all edit targets reachable even when a stopped attack produces no
    // output. The bright baseline marks that case, rather than a false peak.
    if (shape.attackStopped)
      graphics.DrawLine(accent, shape.left, shape.bottom, shape.right, shape.bottom, &mBlend, 2.f);

    constexpr const char* labels[] = {"Attack", "Decay / level", "Sustain", "Release"};
    for (std::size_t i = 0; i < shape.handles.size(); ++i)
    {
      const auto& point = shape.handles[i];
      const bool highlighted = static_cast<int>(i) == mDragHandle ||
                               static_cast<int>(i) == mHoverHandle;
      graphics.FillCircle(highlighted ? IColor(255, 255, 208, 108) : accent,
                          point.x, point.y, highlighted ? 6.f : 5.f, &mBlend);
      graphics.DrawCircle(background, point.x, point.y, highlighted ? 6.f : 5.f, &mBlend);
      graphics.DrawText(caption, labels[i],
        IRECT(point.from - 10, mRECT.B - 17, point.to + 10, mRECT.B - 1), &mBlend);
    }
  }

  int GetValIdxForPos(float x, float) const override
  {
    const float width = std::max(1.f, mRECT.W() - 28.f);
    const float position = (x - mRECT.L - 14.f) / width;
    if (position < .265f) return kAttack;
    if (position < .505f) return kDecay;
    if (position < .755f) return kSustain;
    return kRelease;
  }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    if (IsDisabled() || !mod.L) return;
    // IGraphics begins gestures for the five bound parameters before calling
    // us and ends them after OnMouseUp. Do not duplicate those notifications.
    mPointerGesture = true;
    const auto shape = geometry();
    mDragHandle = hitHandle(shape, x, y);
    if (mDragHandle >= 0)
    {
      mOriginX = x;
      mOriginY = y;
      mStartX = shape.handles[static_cast<std::size_t>(mDragHandle)].x;
      mStartY = shape.handles[static_cast<std::size_t>(mDragHandle)].y;
    }
    SetDirty(false);
  }

  void OnMouseDrag(float x, float y, float, float,
                   const iplug::igraphics::IMouseMod&) override
  {
    updateDrag(x, y);
  }

  void OnMouseUp(float x, float y, const iplug::igraphics::IMouseMod&) override
  {
    updateDrag(x, y);
    mDragHandle = -1;
    mPointerGesture = false; // The framework sends the normal End gestures.
    SetDirty(false);
  }

  void OnTouchCancelled(float, float, const iplug::igraphics::IMouseMod&) override
  {
    // The pinned IGraphics cancel path removes capture without ending edits.
    CancelPointerEdit();
  }

  // Call before closing the editor while the delegate is still alive. Normal
  // mouse-up already closes its gestures; cancellation keeps committed edits.
  void CancelPointerEdit()
  {
    const bool notify = mPointerGesture;
    mPointerGesture = false;
    mDragHandle = -1;
    mHoverHandle = -1;
    if (notify && GetDelegate())
      for (int index = 0; index < NVals(); ++index)
        GetDelegate()->EndInformHostOfParamChangeFromUI(GetParamIdx(index));
    SetDirty(false);
  }

  void OnMouseOver(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    const int next = IsDisabled() ? -1 : hitHandle(geometry(), x, y);
    if (next != mHoverHandle)
    {
      mHoverHandle = next;
      SetDirty(false);
    }
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mHoverHandle = -1;
    SetDirty(false);
  }

private:
  enum ValueIndex { kAttack, kDecay, kLevel, kSustain, kRelease };
  struct Handle
  {
    float x, y, from, to;
    int valueIndex, maximum;
  };
  struct Geometry
  {
    float left, right, top, bottom;
    bool attackStopped, sustainHeld, releaseHeld;
    bool attackExponential, sustainExponential, releaseExponential;
    std::array<Handle, 4> handles;
  };

  int code(int index, int maximum) const
  {
    const double value = GetValue(index);
    return std::isfinite(value) ? static_cast<int>(std::lround(std::clamp(value, 0., 1.) * maximum)) : 0;
  }

  bool mode(int parameter)
  {
    const auto* value = GetDelegate() ? GetDelegate()->GetParam(parameter) : nullptr;
    return value && value->Value() > .5;
  }

  Geometry geometry()
  {
    const int attack = code(kAttack, 127), decay = code(kDecay, 15);
    const int sustain = code(kSustain, 127), release = code(kRelease, 31);
    const bool attackExp = mode(kParamAttackExponential);
    const bool sustainExp = mode(kParamSustainExponential);
    const bool sustainDown = mode(kParamSustainDecrease);
    const bool releaseExp = mode(kParamReleaseExponential);
    const float level = static_cast<float>(code(kLevel, 15) + 1) / 16.f;
    const bool held = sustain >= (sustainExp && !sustainDown ? 100 : 108);
    const float endLevel = held ? level : sustainDown ? 0.f : 1.f;
    Geometry shape{};
    shape.left = mRECT.L + 14.f;
    shape.right = std::max(shape.left + 1.f, mRECT.R - 14.f);
    shape.top = mRECT.T + 23.f;
    shape.bottom = std::max(shape.top + 1.f, mRECT.B - 23.f);
    shape.attackStopped = attack >= (attackExp ? 100 : 108);
    shape.sustainHeld = held;
    shape.releaseHeld = release >= 27;
    shape.attackExponential = attackExp;
    shape.sustainExponential = sustainExp;
    shape.releaseExponential = releaseExp;
    const auto handle = [&](float from, float to, int value, int maximum, int index, float gain) {
      const float width = shape.right - shape.left;
      return Handle{shape.left + (from + (to - from) * value / maximum) * width,
                    shape.bottom - gain * (shape.bottom - shape.top),
                    shape.left + from * width, shape.left + to * width, index, maximum};
    };
    shape.handles = {{handle(.07f, .23f, attack, 127, kAttack, 1.f),
                      handle(.30f, .46f, decay, 15, kDecay, level),
                      handle(.55f, .71f, sustain, 127, kSustain, endLevel),
                      handle(.80f, .96f, release, 31, kRelease, shape.releaseHeld ? endLevel : 0.f)}};
    return shape;
  }

  static int hitHandle(const Geometry& shape, float x, float y)
  {
    if (!std::isfinite(x) || !std::isfinite(y)) return -1;
    float best = 18.f * 18.f;
    int found = -1;
    for (std::size_t i = 0; i < shape.handles.size(); ++i)
    {
      const float dx = x - shape.handles[i].x, dy = y - shape.handles[i].y;
      const float distance = dx * dx + dy * dy;
      if (distance <= best) { best = distance; found = static_cast<int>(i); }
    }
    return found;
  }

  void updateDrag(float x, float y)
  {
    if (IsDisabled() || mDragHandle < 0 || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto shape = geometry();
    const auto& point = shape.handles[static_cast<std::size_t>(mDragHandle)];
    const float targetX = mStartX + x - mOriginX;
    const double proportion = std::clamp(double((targetX - point.from) / (point.to - point.from)), 0., 1.);
    const int rate = static_cast<int>(std::lround(proportion * point.maximum));
    SetValueFromUserInput(double(rate) / point.maximum, point.valueIndex);
    if (mDragHandle == 1)
    {
      const float targetY = mStartY + y - mOriginY;
      const double level = double(shape.bottom - targetY) / (shape.bottom - shape.top);
      const int value = std::clamp(static_cast<int>(std::lround(std::clamp(level, 0., 1.) * 16. - 1.)), 0, 15);
      SetValueFromUserInput(double(value) / 15., kLevel);
    }
  }

  int mDragHandle = -1;
  int mHoverHandle = -1;
  bool mPointerGesture = false;
  float mOriginX = 0, mOriginY = 0, mStartX = 0, mStartY = 0;
};
