// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Editor-only authoring controls. File reads and native preparation belong to
// VabLoader; audio sees only an already-prepared exchange publication.
#include "TetoricaSPU.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include "EnvelopeControl.h"
#include <algorithm>
#include <array>
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

constexpr std::array<const char*, 8> kReverbNames{{"Off", "Room", "Studio Small",
  "Studio Medium", "Studio Large", "Hall", "Space Echo", "Half Echo"}};

void editParameter(TetoricaSPU& plugin, int parameter, double value)
{
  const double normalized = plugin.GetParam(parameter)->ToNormalized(value);
  plugin.BeginInformHostOfParamChangeFromUI(parameter);
  plugin.SendParameterValueFromUI(parameter, normalized);
  plugin.SendParameterValueFromDelegate(parameter, normalized, true);
  plugin.EndInformHostOfParamChangeFromUI(parameter);
}

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
  bool reverbPage = true;
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
    const auto smallStyle = DEFAULT_STYLE.WithLabelText(IText(13))
      .WithValueText(IText(14)).WithDrawShadows(false);
    const auto switchStyle = smallStyle.WithShowLabel(false);
    graphics->AttachControl(new IVButtonControl(IRECT(560, 162, 624, 196),
      [this](IControl*) { showInstrumentPage(false); }, "ADSR", smallStyle), kCtrlPageEnvelope);
    graphics->AttachControl(new IVButtonControl(IRECT(632, 162, 696, 196),
      [this](IControl*) { showInstrumentPage(true); }, "Reverb", smallStyle), kCtrlPageReverb);
    graphics->AttachControl(new IVToggleControl(IRECT(24, 202, 188, 230),
      kParamEnvelopeEnabled, "", switchStyle, "Tone ADSR", "Custom ADSR"), kCtrlEnvelopeEnabled);
    auto* copyEnvelope = new IVButtonControl(IRECT(200, 202, 336, 230),
      [this](IControl*) { copyToneEnvelopeFromUI(); }, "Copy tone ADSR", smallStyle);
    copyEnvelope->SetTooltip("Copy the original Tone's ADSR and enable Custom ADSR.");
    graphics->AttachControl(copyEnvelope, kCtrlEnvelopeCopy);
    auto* liveEnvelope = new IVToggleControl(IRECT(348, 202, 696, 230),
      kParamEnvelopeLive, "", switchStyle,
      "Apply to sounding notes: OFF", "Apply to sounding notes: ON");
    liveEnvelope->SetTooltip("On applies the latest ADSR from each note's current level and sample position. Off keeps existing notes unchanged through Release; new notes use the edits.");
    graphics->AttachControl(liveEnvelope, kCtrlEnvelopeLive);
    graphics->AttachControl(new SpuEnvelopeGraphControl(IRECT(24, 238, 696, 326)),
      kCtrlEnvelopeGraph);
    const std::array<int, 5> rateParams{{kParamAttackRate, kParamDecayRate,
      kParamSustainLevel, kParamSustainRate, kParamReleaseRate}};
    const std::array<const char*, 5> rateNames{{"Attack rate", "Decay rate",
      "Sustain level", "Sustain rate", "Release rate"}};
    for (int index = 0; index < 5; ++index)
    {
      const float x = 24.f + index * 136.f;
      auto* knob = new IVKnobControl(IRECT(x, 330, x + 112, 412),
        rateParams[index], rateNames[index], smallStyle, true);
      knob->SetTooltip(index == 2 ? "Sustain threshold: (code + 1) / 16; zero is not silence." :
        "Raw SPU rate code, not milliseconds. Zero is fastest; larger codes are slower or hold.");
      graphics->AttachControl(knob, kCtrlEnvelopeFirst + index);
    }
    const std::array<int, 4> modeParams{{kParamAttackExponential,
      kParamSustainDecrease, kParamSustainExponential, kParamReleaseExponential}};
    const std::array<const char*, 4> modeOff{{"Attack: linear", "Sustain: up",
      "Sustain: linear", "Release: linear"}};
    const std::array<const char*, 4> modeOn{{"Attack: exp", "Sustain: down",
      "Sustain: exp", "Release: exp"}};
    for (int index = 0; index < 4; ++index)
    {
      const float x = 24.f + index * 172.f;
      graphics->AttachControl(new IVToggleControl(IRECT(x, 418, x + 156, 446),
        modeParams[index], "", switchStyle, modeOff[index], modeOn[index]),
        kCtrlEnvelopeFirst + 5 + index);
    }
    graphics->AttachControl(new ITextControl(IRECT(24, 449, 696, 469),
      "Custom preview - enable Custom ADSR to use the edits.",
      IText(12).WithAlign(EAlign::Near)), kCtrlEnvelopeStatus);

    graphics->AttachControl(new ITextControl(IRECT(24, 205, 696, 238),
      "SPU Reverb", IText(22).WithAlign(EAlign::Near)), kCtrlReverbHeading);
    auto* presetMenu = new SpuMenuControl(IRECT(24, 248, 424, 286), "Shared preset: Off",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        for (int preset = 0; preset < static_cast<int>(kReverbNames.size()); ++preset)
          menu.AddItem(new IPopupMenu::Item(kReverbNames[preset],
            GetParam(kParamReverbPreset)->Int() == preset ? IPopupMenu::Item::kChecked : 0, preset));
        choice = [this](int preset) { chooseReverbPresetFromUI(preset); };
      });
    presetMenu->SetTooltip("Selecting a preset enables reverb. Off silences and clears the shared effect.");
    graphics->AttachControl(presetMenu, kCtrlReverbPreset);
    graphics->AttachControl(new IVToggleControl(IRECT(440, 248, 696, 286),
      kParamReverbEnabled, "", switchStyle, "Enable reverb: OFF", "Enable reverb: ON"), kCtrlReverbEnabled);
    graphics->AttachControl(new IVKnobControl(IRECT(56, 298, 244, 414),
      kParamReverbLeft, "Return left", smallStyle, true), kCtrlReverbLeft);
    graphics->AttachControl(new IVKnobControl(IRECT(272, 298, 460, 414),
      kParamReverbRight, "Return right", smallStyle, true), kCtrlReverbRight);
    auto* toneSend = new IVToggleControl(IRECT(488, 317, 696, 358),
      kParamReverbToneSend, "", switchStyle, "Send: all notes", "Send: Tone setting");
    toneSend->SetTooltip("All notes sends every keyboard voice to reverb. Tone setting follows the original VAB send flag; Builtin Sine has no original send.");
    graphics->AttachControl(toneSend, kCtrlReverbToneSend);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(24, 420, 696, 446),
      "One shared effect for all voices. Named core approximations of PS1 reverb.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlReverbInfo);
    graphics->AttachControl(new ITextControl(IRECT(24, 449, 696, 469),
      "Off: dry output. Choose a preset to enable reverb.",
      IText(12).WithAlign(EAlign::Near)), kCtrlReverbStatus);
    graphics->AttachControl(new SpuKeyboardControl(IRECT(24, 478, 696, 602), 48, 84),
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
  if (auto* graphics = GetUI())
    if (auto* graph = graphics->GetControlWithTag(kCtrlEnvelopeGraph))
      static_cast<SpuEnvelopeGraphControl*>(graph)->CancelPointerEdit();
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

  const bool custom = GetParam(kParamEnvelopeEnabled)->Bool();
  for (int tag = kCtrlEnvelopeFirst; tag <= kCtrlEnvelopeLast; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsDisabled() == custom) control->SetDisabled(!custom);
  if (auto* graph = graphics->GetControlWithTag(kCtrlEnvelopeGraph))
  {
    if (graph->IsDisabled() == custom) graph->SetDisabled(!custom);
    graph->SetDirty(false);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlEnvelopeStatus))
  {
    const char* message = !custom ? "Custom preview is disabled. New notes use the original Tone ADSR." :
      GetParam(kParamReleaseRate)->Int() >= 27 ?
      "Release 27-31 holds indefinitely. Use All sound off to stop immediately." :
      "Rate codes: 0 = fastest. Drag A / D / S / R; drag D vertically for sustain level.";
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
  }

  for (int tag = kCtrlEnvelopeGraph; tag <= kCtrlEnvelopeLive; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() != ui.reverbPage) control->Hide(ui.reverbPage);
  for (int tag = kCtrlReverbHeading; tag <= kCtrlReverbStatus; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == ui.reverbPage) control->Hide(!ui.reverbPage);
  for (const auto tag : {kCtrlPageEnvelope, kCtrlPageReverb})
    if (auto* control = graphics->GetControlWithTag(tag))
    {
      const char* label = tag == kCtrlPageReverb ?
        (ui.reverbPage ? "> Reverb" : "Reverb") : (ui.reverbPage ? "ADSR" : "> ADSR");
      auto* button = static_cast<IVButtonControl*>(control);
      if (std::strcmp(button->GetLabelStr(), label)) button->SetLabelStr(label);
    }
  const int preset = std::clamp(GetParam(kParamReverbPreset)->Int(), 0, 7);
  if (auto* control = graphics->GetControlWithTag(kCtrlReverbPreset))
  {
    const auto label = std::string("Shared preset: ") + kReverbNames[preset];
    auto* menu = static_cast<SpuMenuControl*>(control);
    if (std::strcmp(menu->GetLabelStr(), label.c_str())) menu->SetLabelStr(label.c_str());
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlReverbStatus))
  {
    bool sourceSend = false;
    if (vab)
      for (const auto& tone : ui.bank->tones)
        if (tone.program == ui.definition->program() && tone.index == ui.definition->tone())
        { sourceSend = (tone.mode & 4) != 0; break; }
    const char* message = preset == 0 ? "Off: dry output. Choose a preset to enable reverb." :
      !GetParam(kParamReverbEnabled)->Bool() ? "Bypassed. Preset and return levels are kept." :
      GetParam(kParamReverbToneSend)->Bool() && !sourceSend ?
        "This Tone has no reverb send. Choose All notes to hear reverb." :
      GetParam(kParamReverbLeft)->Value() == 0 && GetParam(kParamReverbRight)->Value() == 0 ?
        "Both returns are 0%. Raise a return to hear reverb." :
        "Reverb enabled. Returns add wet sound to the original dry sound.";
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
  }
}

void TetoricaSPU::showInstrumentPage(bool reverb)
{
  if (!GetUI() || !mInstrumentUI) return;
  if (reverb)
    if (auto* graph = GetUI()->GetControlWithTag(kCtrlEnvelopeGraph))
      static_cast<SpuEnvelopeGraphControl*>(graph)->CancelPointerEdit();
  mInstrumentUI->reverbPage = reverb;
  updateInstrumentControls();
}

void TetoricaSPU::chooseReverbPresetFromUI(int preset)
{
  if (!GetUI() || preset < 0 || preset >= static_cast<int>(kReverbNames.size())) return;
  editParameter(*this, kParamReverbPreset, preset);
  editParameter(*this, kParamReverbEnabled, preset != 0 ? 1. : 0.);
  updateInstrumentControls();
}

void TetoricaSPU::copyToneEnvelopeFromUI()
{
  if (!GetUI() || !mInstrumentUI) return;
  // The UI already owns parsed authoring metadata. Do not read the live SPU or
  // request an engine replacement merely to copy envelope parameters.
  tetorica::EnvelopeRegisters envelope;
  const auto& ui = *mInstrumentUI;
  if (ui.definition && ui.bank)
    for (const auto& tone : ui.bank->tones)
      if (tone.program == ui.definition->program() && tone.index == ui.definition->tone())
      { envelope = {tone.adsr1, tone.adsr2}; break; }
  const std::array<std::pair<int, double>, 10> edits{{
    {kParamAttackRate, double((envelope.adsr1 >> 8) & 127)},
    {kParamAttackExponential, double((envelope.adsr1 >> 15) & 1)},
    {kParamDecayRate, double((envelope.adsr1 >> 4) & 15)},
    {kParamSustainLevel, double(envelope.adsr1 & 15)},
    {kParamSustainRate, double((envelope.adsr2 >> 6) & 127)},
    {kParamSustainDecrease, double((envelope.adsr2 >> 14) & 1)},
    {kParamSustainExponential, double((envelope.adsr2 >> 15) & 1)},
    {kParamReleaseRate, double(envelope.adsr2 & 31)},
    {kParamReleaseExponential, double((envelope.adsr2 >> 5) & 1)},
    {kParamEnvelopeEnabled, 1.}
  }};
  for (const auto& edit : edits)
    editParameter(*this, edit.first, edit.second);
  updateInstrumentControls();
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
