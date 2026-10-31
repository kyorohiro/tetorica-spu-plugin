// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Editor-only authoring controls. File reads and native preparation belong to
// VabLoader; audio sees only an already-prepared exchange publication.
#include "TetoricaSPU.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#if defined(VST3_API)
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#endif

namespace {
using namespace iplug;
using namespace iplug::igraphics;

class SpuKeyboardControl final : public IVKeyboardControl
{
public:
  using IVKeyboardControl::IVKeyboardControl;
  void OnMouseOut() override
  {
    OnTouchCancelled(0.f, 0.f, IMouseMod{});
    IVKeyboardControl::OnMouseOut();
  }
};

// These menus describe a particular bank, rather than an automatable enum.
// The menu and its choice callback persist while the editor popup is open.
class SpuMenuControl final : public IVButtonControl
{
public:
  using Choice = std::function<void(int)>;
  using Populate = std::function<void(IPopupMenu&, Choice&)>;
  SpuMenuControl(const IRECT& bounds, const char* label, Populate populate)
  : IVButtonControl(bounds, nullptr, label), mPopulate(std::move(populate)) {}

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    mMenu.Clear();
    mChoice = {};
    mPopulate(mMenu, mChoice);
    if (mMenu.NItems())
      GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
    SetDirty(false);
  }

  void OnPopupMenuSelection(IPopupMenu* menu, int) override
  {
    if (menu)
      if (const auto* item = menu->GetChosenItem())
        if (item->GetIsChoosable() && mChoice)
          mChoice(item->GetTag());
    SetDirty(false);
  }

private:
  IPopupMenu mMenu;
  Populate mPopulate;
  Choice mChoice;
};

bool selectable(const tetorica::vab::Tone& tone, const tetorica::vab::Bank& bank)
{
  return tone.sample > 0 && tone.sample != 255 && tone.sample <= bank.samples.size();
}

std::string toneLabel(const tetorica::vab::Tone& tone)
{
  auto label = std::string("Tone ") + std::to_string(tone.index) +
    "  /  Sample " + std::to_string(tone.sample);
  if (!tone.sample) label += " (unassigned)";
  if (tone.sample == 255) label += " (noise unsupported)";
  return label;
}

bool sameDefinition(const std::shared_ptr<const tetorica::InstrumentDefinition>& a,
                    const std::shared_ptr<const tetorica::InstrumentDefinition>& b)
{
  if (a == b) return true;
  if (!a || !b || a->kind() != b->kind() ||
      a->program() != b->program() || a->tone() != b->tone()) return false;
  return a->bankBytes() == b->bankBytes();
}
} // namespace

struct TetoricaSPU::InstrumentUIState
{
  struct Lifetime
  {
    std::mutex mutex;
    TetoricaSPU* owner = nullptr;
  };
  std::shared_ptr<Lifetime> lifetime;
  std::shared_ptr<const tetorica::InstrumentDefinition> definition;
  std::shared_ptr<const tetorica::InstrumentDefinition> notificationBaseline;
  std::optional<tetorica::vab::Bank> bank;
  tetorica::SpuEngineExchange::Generation requested = 0;
  std::uint64_t revision = 0;
  bool baselineInitialized = false;
  bool pickerOpen = false;
  std::string status = "Play MIDI from your DAW or the keyboard.";
};

void TetoricaSPU::initializeInstrumentUI()
{
  using namespace iplug;
  using namespace igraphics;
  mInstrumentUI = std::make_shared<InstrumentUIState>();
  mLayoutFunc = [this](IGraphics* graphics) {
    graphics->LoadFont("Roboto-Regular", ROBOTO_FN);
    // Keep menus inside the editor. The pinned native macOS popup schedules a
    // callback retaining a raw graphics pointer beyond editor destruction.
    graphics->AttachPopupMenuControl(IText(15));
    graphics->AttachPanelBackground(IColor(255, 239, 248, 250));
    graphics->AttachControl(new ITextControl(IRECT(24, 18, 550, 58),
      "Tetorica SPU", IText(30, IColor(255, 27, 62, 73))));
    graphics->AttachControl(new IVButtonControl(IRECT(24, 64, 154, 99),
      [this](IControl*) { promptForVab(); }, "Load VAB..."), kCtrlLoadVab);
    graphics->AttachControl(new IVButtonControl(IRECT(166, 64, 296, 99),
      [this](IControl*) { requestBuiltinFromUI(); }, "Builtin Sine"), kCtrlBuiltin);
    graphics->AttachControl(new ITextControl(IRECT(306, 64, 550, 99),
      "Builtin Sine", IText(14)), kCtrlBankLabel);
    graphics->AttachControl(new SpuMenuControl(IRECT(24, 108, 276, 144), "Program",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        const auto& ui = *mInstrumentUI;
        if (!ui.bank || !ui.definition) return;
        const auto source = ui.definition;
        const auto revision = mInstrumentUIRevision.load(std::memory_order_acquire);
        const auto generation = mEngines.latestGeneration();
        // Capture the selected bank. A later restore/publication must not make
        // a menu opened for an earlier bank select an unrelated Program ID.
        for (const auto& program : ui.bank->programs)
        {
          const auto playable = std::find_if(program.toneIndices.begin(), program.toneIndices.end(),
            [&](std::size_t flat) { return selectable(ui.bank->tones.at(flat), *ui.bank); });
          int flags = program.index == source->program() ? IPopupMenu::Item::kChecked : 0;
          if (playable == program.toneIndices.end()) flags |= IPopupMenu::Item::kDisabled;
          const auto label = std::string("Program ") + std::to_string(program.index);
          menu.AddItem(new IPopupMenu::Item(label.c_str(), flags, program.index));
        }
        choice = [this, source, revision, generation](int programId) {
          requestProgramFromUI(source, revision, generation, static_cast<std::uint32_t>(programId));
        };
      }), kCtrlProgram);
    graphics->AttachControl(new SpuMenuControl(IRECT(288, 108, 550, 144), "Tone",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        const auto& ui = *mInstrumentUI;
        if (!ui.bank || !ui.definition) return;
        const auto source = ui.definition;
        const auto revision = mInstrumentUIRevision.load(std::memory_order_acquire);
        const auto generation = mEngines.latestGeneration();
        for (const auto& tone : ui.bank->tones)
          if (tone.program == source->program())
          {
            int flags = tone.index == source->tone() ? IPopupMenu::Item::kChecked : 0;
            if (!selectable(tone, *ui.bank)) flags |= IPopupMenu::Item::kDisabled;
            const auto label = toneLabel(tone);
            menu.AddItem(new IPopupMenu::Item(label.c_str(), flags, tone.index));
          }
        choice = [this, source, revision, generation](int tone) {
          requestSelectionFromUI(source, revision, generation, source->program(), static_cast<std::uint32_t>(tone));
        };
      }), kCtrlTone);
    graphics->AttachControl(new IVKnobControl(IRECT(576, 28, 696, 152),
      kParamGain, "Gain"));
    graphics->AttachControl(new IVButtonControl(IRECT(24, 154, 154, 189),
      [this](IControl*) { SendMidiMsgFromUI(IMidiMsg(0, 0xb0, 120, 0)); },
      "All sound off"));
    graphics->AttachControl(new IMultiLineTextControl(IRECT(166, 150, 550, 199),
      mInstrumentUI->status.c_str(), IText(13).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)),
      kCtrlInstrumentStatus);
    graphics->AttachControl(new SpuKeyboardControl(IRECT(24, 206, 696, 336), 48, 84),
      kCtrlKeyboard);
    updateInstrumentControls();
  };
}

void TetoricaSPU::OnUIOpen()
{
  iplug::Plugin::OnUIOpen();
  closeInstrumentUI();
  mInstrumentUI->lifetime = std::make_shared<InstrumentUIState::Lifetime>();
  mInstrumentUI->lifetime->owner = this;
  mInstrumentUI->pickerOpen = false;
  OnIdle();
  updateInstrumentControls();
}

void TetoricaSPU::closeInstrumentUI() noexcept
{
  if (mInstrumentUI && mInstrumentUI->lifetime)
  {
    const auto lifetime = mInstrumentUI->lifetime;
    {
      std::lock_guard<std::mutex> lock(lifetime->mutex);
      lifetime->owner = nullptr;
    }
    mInstrumentUI->lifetime.reset();
  }
}

void TetoricaSPU::OnUIClose()
{
  closeInstrumentUI();
  iplug::Plugin::OnUIClose();
}

void TetoricaSPU::promptForVab()
{
  using namespace iplug::igraphics;
  if (!GetUI() || mInstrumentUI->pickerOpen || !mInstrumentUI->lifetime) return;
  mInstrumentUI->pickerOpen = true;
  updateInstrumentControls();
  const std::weak_ptr<InstrumentUIState::Lifetime> lifetime = mInstrumentUI->lifetime;
  const auto revision = mInstrumentUIRevision.load(std::memory_order_acquire);
  const auto generation = mEngines.latestGeneration();
  WDL_String fileName, path;
  GetUI()->PromptForFile(fileName, path, EFileAction::Open, "vab",
    [lifetime, revision, generation](const WDL_String& file, const WDL_String&) {
      const auto live = lifetime.lock();
      if (!live) return;
      // The callback can outlive its editor. The destructor invalidates this
      // token under the same mutex before any instance members are destroyed.
      std::lock_guard<std::mutex> lifetimeLock(live->mutex);
      auto* owner = live->owner;
      if (!owner) return;
      auto& ui = *owner->mInstrumentUI;
      ui.pickerOpen = false;
      if (file.GetLength())
      {
        try
        {
          std::lock_guard<std::mutex> stateLock(owner->mStateMutex);
          if (owner->mInstrumentUIRevision.load(std::memory_order_acquire) == revision &&
              owner->mEngines.latestGeneration() == generation)
          {
            ui.requested = owner->mLoader.requestFile(std::string(file.Get()));
            ui.status = "Loading VAB. Previous sound remains until ready.";
          }
        }
        catch (const std::exception& error) { ui.status = std::string("Load failed: ") + error.what(); }
      }
      owner->updateInstrumentControls();
    });
}

void TetoricaSPU::requestBuiltinFromUI()
{
  try
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mInstrumentUI->requested = mLoader.requestBuiltin();
    mInstrumentUI->status = "Preparing Builtin Sine.";
  }
  catch (const std::exception& error) { mInstrumentUI->status = error.what(); }
  updateInstrumentControls();
}

void TetoricaSPU::requestSelectionFromUI(
  std::shared_ptr<const tetorica::InstrumentDefinition> source,
  std::uint64_t revision, tetorica::SpuEngineExchange::Generation generation,
  std::uint32_t program, std::uint32_t tone)
{
  try
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    if (mInstrumentUIRevision.load(std::memory_order_acquire) != revision ||
        mEngines.latestGeneration() != generation ||
        mEngines.snapshotDefinition() != source)
      return;
    if (source->program() == program && source->tone() == tone) return;
    mInstrumentUI->requested = mLoader.requestSelection(std::move(source), program, tone);
    mInstrumentUI->status = "Preparing tone. Previous sound remains until ready.";
  }
  catch (const std::exception& error) { mInstrumentUI->status = error.what(); }
  updateInstrumentControls();
}

void TetoricaSPU::requestProgramFromUI(
  std::shared_ptr<const tetorica::InstrumentDefinition> source,
  std::uint64_t revision, tetorica::SpuEngineExchange::Generation generation,
  std::uint32_t program)
{
  try
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    if (mInstrumentUIRevision.load(std::memory_order_acquire) != revision ||
        mEngines.latestGeneration() != generation ||
        mEngines.snapshotDefinition() != source || source->program() == program)
      return;
    // The worker finds a genuinely playable tone within this Program, including
    // native SDK loop restrictions. Metadata alone cannot determine support.
    mInstrumentUI->requested = mLoader.requestProgram(std::move(source), program);
    mInstrumentUI->status = "Preparing program. Previous sound remains until ready.";
  }
  catch (const std::exception& error) { mInstrumentUI->status = error.what(); }
  updateInstrumentControls();
}

void TetoricaSPU::OnIdle()
{
  if (!mInstrumentUI) return;
  auto& ui = *mInstrumentUI;
  std::shared_ptr<const tetorica::InstrumentDefinition> definition;
  bool notify = false;
  std::uint64_t revision = 0;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    revision = mInstrumentUIRevision.load(std::memory_order_acquire);
    const bool restored = ui.revision != revision;
    if (restored)
    {
      ui.revision = revision;
      ui.requested = 0;
      ui.status = "Project sound restored.";
    }
    const auto status = mLoader.status();
    const auto latest = mEngines.latestGeneration();
    if (ui.requested && ui.requested == latest && status.generation == ui.requested)
    {
      if (status.state == tetorica::VabLoader::State::Ready)
      {
        ui.status = mEngines.adoptedGeneration() == ui.requested ?
          "Loaded. Play MIDI or the keyboard." : "Ready. Waiting for an audio block.";
      }
      else if (status.state == tetorica::VabLoader::State::Failed)
        ui.status = std::string("Load failed: ") + status.error + ". Previous sound kept.";
    }
    else if (ui.requested && ui.requested != latest)
      ui.requested = 0;
    definition = mEngines.snapshotDefinition();
    // Follow the surviving authoring definition, rather than only the latest
    // job status. An adopted successful load still changed state when a newer
    // request fails before the UI had a chance to observe that success.
    if (ui.baselineInitialized && !restored)
      notify = !sameDefinition(ui.notificationBaseline, definition);
    ui.notificationBaseline = definition;
    ui.baselineInitialized = true;
  }
  if (definition != ui.definition)
  {
    try
    {
      if (definition && definition->kind() == tetorica::InstrumentDefinition::Kind::Vab)
      {
        // Reuse parsed metadata for selection changes within the same bank.
        // This bounded parse is non-RT and runs only on a new bank or restore.
        if (!ui.definition || !ui.bank ||
            ui.definition->bankBytes() != definition->bankBytes())
          ui.bank = tetorica::vab::parse({definition->bankBytes().data(), definition->bankBytes().size()});
      }
      else ui.bank.reset();
      ui.definition = std::move(definition);
    }
    catch (const std::exception& error)
    {
      ui.bank.reset();
      ui.definition.reset();
      ui.status = std::string("Bank display failed: ") + error.what();
    }
  }
  updateInstrumentControls();
  // Never call host callbacks with a state/loader mutex held: hosts may save
  // synchronously in response. Do not mark a superseding host restore dirty.
  if (notify && mInstrumentUIRevision.load(std::memory_order_acquire) == revision)
    notifyHostOfInstrumentEdit();
}

void TetoricaSPU::updateInstrumentControls()
{
  auto* graphics = GetUI();
  if (!graphics || !mInstrumentUI) return;
  const auto& ui = *mInstrumentUI;
  const bool vab = ui.definition && ui.bank &&
    ui.definition->kind() == tetorica::InstrumentDefinition::Kind::Vab;
  if (auto* control = graphics->GetControlWithTag(kCtrlLoadVab))
    if (control->IsDisabled() != ui.pickerOpen) control->SetDisabled(ui.pickerOpen);
  if (auto* control = graphics->GetControlWithTag(kCtrlBankLabel))
    static_cast<iplug::igraphics::ITextControl*>(control)->SetStr(vab ? "Embedded VAB" : "Builtin Sine");
  if (auto* control = graphics->GetControlWithTag(kCtrlInstrumentStatus))
  {
    auto* text = static_cast<iplug::igraphics::ITextControl*>(control);
    if (std::strcmp(text->GetStr(), ui.status.c_str()))
    {
      text->SetStr(ui.status.c_str());
      control->SetTooltip(ui.status.c_str());
    }
  }
  for (const auto tag : {kCtrlProgram, kCtrlTone})
    if (auto* control = graphics->GetControlWithTag(tag))
    {
      if (control->IsDisabled() == vab) control->SetDisabled(!vab);
      const auto label = vab ?
        (tag == kCtrlProgram ? "Program " + std::to_string(ui.definition->program()) :
          "Tone " + std::to_string(ui.definition->tone())) :
        (tag == kCtrlProgram ? std::string("Program (VAB only)") : std::string("Tone (VAB only)"));
      auto* menu = static_cast<SpuMenuControl*>(control);
      if (std::strcmp(menu->GetLabelStr(), label.c_str())) menu->SetLabelStr(label.c_str());
    }
}

void TetoricaSPU::notifyHostOfInstrumentEdit()
{
#if defined(VST3_API)
  Steinberg::FUnknownPtr<Steinberg::Vst::IComponentHandler2> handler(GetComponentHandler());
  if (handler) handler->setDirty(true);
#elif defined(AU_API)
  // The pinned AU base keeps InformListeners private. Its public preset-change
  // notification is the available host hook; project dirty behavior varies by
  // AU host, while GetState always supplies the full embedded definition.
  InformHostOfPresetChange();
#endif
}
#endif // IPLUG_EDITOR
