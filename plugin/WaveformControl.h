// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

// Editor-only source PCM. A stroke changes a local draft once on mouse-up;
// neither pointer motion nor this control publishes an engine or host state.
#include "SpuWaveform.h"
#include "SpuMorph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

class SpuWaveformControl final : public iplug::igraphics::IControl
{
public:
  using Change = std::function<void(const tetorica::PcmCycle&)>;

  SpuWaveformControl(const iplug::igraphics::IRECT& bounds, Change change)
  : IControl(bounds), mChange(std::move(change)),
    mCycle(tetorica::makePcmCycle(tetorica::WaveformShape::Sine)), mStroke(mCycle)
  {
    mDblAsSingleClick = true;
    SetTooltip("Draw the 112-sample source PCM. Release completes one edit; Auto apply is on by default. With Auto apply off, press Apply to hear the draft. Replacement stops notes and reverb tails. Cancelled strokes are discarded.");
  }

  void SetAutoApply(bool enabled)
  {
    if (mAutoApply == enabled) return;
    mAutoApply = enabled;
    SetDirty(false);
  }

  void SetCycle(const tetorica::PcmCycle& cycle)
  {
    if (!mMorph && cycle == mCycle) return;
    CancelPointerEdit();
    mMorph = false;
    mCycle = cycle;
    SetDirty(false);
  }

  void SetMorph(const tetorica::MorphEndpoints& endpoints, double amount,
                bool editB, bool draft)
  {
    // A host may transiently supply an invalid plain parameter. Rendering the
    // editor must stay safe while the audio layer rejects that tuple.
    amount = std::isfinite(amount) ? std::clamp(amount, 0., 1.) : mAmount;
    const bool endpointsChanged = !mMorph || endpoints.a != mEndpoints.a ||
      endpoints.b != mEndpoints.b || editB != mEditB;
    const bool changed = endpointsChanged || amount != mAmount || draft != mDraft;
    if (!changed) return;
    if (endpointsChanged) CancelPointerEdit();
    mMorph = true; mEndpoints = endpoints; mEditB = editB;
    mAmount = amount; mDraft = draft;
    mCycle = editB ? endpoints.b : endpoints.a;
    SetDirty(false);
  }

  void CancelPointerEdit()
  {
    if (!mDrawing) return;
    mDrawing = false;
    mLastIndex = -1;
    SetDirty(false);
  }

  void Draw(iplug::igraphics::IGraphics& graphics) override
  {
    using namespace iplug::igraphics;
    const IColor background(255, 22, 34, 42), grid(255, 51, 68, 78);
    const IColor accent(255, 116, 195, 234), text(255, 188, 204, 213);
    const auto plot = plotBounds();
    graphics.FillRect(background, mRECT, &mBlend);
    graphics.DrawRect(grid, mRECT, &mBlend);
    for (float value : {-1.f, 0.f, 1.f})
      graphics.DrawLine(grid, plot.L, toY(value, plot), plot.R, toY(value, plot), &mBlend);
    const auto& cycle = mDrawing ? mStroke : mCycle;
    const auto path = [&](const tetorica::PcmCycle& pcm, const IColor& color,
                          float width, bool dashed) {
      graphics.PathClear();
      for (std::size_t index = 0; index < pcm.size(); ++index)
      {
        const float x = plot.L + plot.W() * float(index) / float(pcm.size() - 1);
        const float y = toY(pcm[index], plot);
        if (index) graphics.PathLineTo(x, y);
        else graphics.PathMoveTo(x, y);
      }
      IStrokeOptions stroke;
      float dashes[] = {6.f, 4.f};
      if (dashed) stroke.mDash.SetDash(dashes, 0.f, 2);
      graphics.PathStroke(IPattern(color), width, stroke, &mBlend);
    };
    if (mMorph)
    {
      auto endpoints = mEndpoints;
      (mEditB ? endpoints.b : endpoints.a) = cycle;
      const IColor waveA(255, 123, 188, 247), waveB(255, 239, 154, 194), result(255, 232, 185, 86);
      path(tetorica::blendMorph(endpoints, mAmount), result, 1.5f, true);
      path(mEditB ? endpoints.a : endpoints.b, mEditB ? waveA : waveB, 1.5f, false);
      path(cycle, mEditB ? waveB : waveA, 3.f, false);
    }
    else path(cycle, accent, 2.f, false);
    const IText caption(11, text);
    const char* label = !mMorph ? (mAutoApply ? "Source PCM / auto apply" : "Source PCM / manual draft") :
      mEditB ? "Edit B (pink) / A (blue) / Result (gold)" : "Edit A (blue) / B (pink) / Result (gold)";
    graphics.DrawText(caption.WithAlign(EAlign::Near), label,
      IRECT(mRECT.L + 10, mRECT.T + 2, mRECT.MW(), mRECT.T + 18), &mBlend);
    char sample[80];
    if (mHoverIndex >= 0)
      std::snprintf(sample, sizeof(sample), "Sample %d: %.4f", mHoverIndex, cycle[std::size_t(mHoverIndex)]);
    else
      std::snprintf(sample, sizeof(sample), "%s", mMorph ?
        (mDraft ? (mAutoApply ? "Auto apply after edit" : "Draft preview / Apply to hear") : "Applied endpoints / live Amount") : "112 samples / -1 to +1");
    graphics.DrawText(caption.WithAlign(EAlign::Far), sample,
      IRECT(mRECT.MW(), mRECT.T + 2, mRECT.R - 10, mRECT.T + 18), &mBlend);
  }

  void OnMouseDown(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    if (IsDisabled() || IsHidden() || !mod.L || !std::isfinite(x) || !std::isfinite(y)) return;
    mDrawing = true;
    mStroke = mCycle;
    mLastIndex = -1;
    drawPoint(x, y);
  }

  void OnMouseDrag(float x, float y, float, float,
                   const iplug::igraphics::IMouseMod&) override
  {
    if (IsDisabled() || IsHidden()) { CancelPointerEdit(); return; }
    drawPoint(x, y);
  }

  void OnMouseUp(float x, float y, const iplug::igraphics::IMouseMod&) override
  {
    if (!mDrawing) return;
    if (IsDisabled() || IsHidden()) { CancelPointerEdit(); return; }
    drawPoint(x, y);
    mDrawing = false;
    mLastIndex = -1;
    if (mStroke != mCycle)
    {
      mCycle = mStroke;
      if (mChange) mChange(mCycle);
    }
    SetDirty(false);
  }

  void OnTouchCancelled(float, float, const iplug::igraphics::IMouseMod&) override
  {
    CancelPointerEdit();
  }

  void OnMouseOver(float x, float y, const iplug::igraphics::IMouseMod& mod) override
  {
    IControl::OnMouseOver(x, y, mod);
    if (!std::isfinite(x)) return;
    mHoverIndex = indexAt(x);
    SetDirty(false);
  }

  void OnMouseOut() override
  {
    IControl::OnMouseOut();
    mHoverIndex = -1;
    SetDirty(false);
  }

private:
  iplug::igraphics::IRECT plotBounds() const
  {
    return {mRECT.L + 10, mRECT.T + 23, mRECT.R - 10, mRECT.B - 10};
  }

  static float toY(float value, const iplug::igraphics::IRECT& plot)
  {
    return plot.T + (1.f - value) * .5f * plot.H();
  }

  int indexAt(float x) const
  {
    const auto plot = plotBounds();
    const float position = (x - plot.L) / std::max(1.f, plot.W());
    return static_cast<int>(std::floor(std::clamp(position, 0.f, 1.f) * 111.f));
  }

  void drawPoint(float x, float y)
  {
    if (!mDrawing || !std::isfinite(x) || !std::isfinite(y)) return;
    const auto plot = plotBounds();
    const int index = indexAt(x);
    const float value = std::clamp(1.f - 2.f * (y - plot.T) / std::max(1.f, plot.H()), -1.f, 1.f);
    const int from = mLastIndex < 0 ? index : mLastIndex;
    const float previous = mLastIndex < 0 ? value : mLastValue;
    for (int at = std::min(from, index); at <= std::max(from, index); ++at)
      mStroke[std::size_t(at)] = from == index ? value :
        previous + (value - previous) * float(at - from) / float(index - from);
    mLastIndex = mHoverIndex = index;
    mLastValue = value;
    SetDirty(false);
  }

  Change mChange;
  tetorica::PcmCycle mCycle, mStroke;
  tetorica::MorphEndpoints mEndpoints{};
  bool mMorph = false, mEditB = false, mDraft = false, mAutoApply = true;
  double mAmount = 0;
  bool mDrawing = false;
  int mLastIndex = -1, mHoverIndex = -1;
  float mLastValue = 0;
};
