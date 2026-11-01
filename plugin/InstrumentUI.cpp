// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Editor-only authoring controls. File reads and native preparation belong to
// VabLoader; audio sees only an already-prepared exchange publication.
#include "TetoricaSPU.h"

#if IPLUG_EDITOR
#include "IControls.h"
#include "EnvelopeControl.h"
#include "WaveformControl.h"
#include "PcmDraftState.h"
#include "MorphDraftState.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
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
constexpr std::array<const char*, 4> kPmonWaveNames{{"Sine", "Triangle", "Saw", "Square"}};
enum class SourceMode { Sample, Noise, Pmon };

SourceMode effectiveSourceMode(TetoricaSPU& plugin)
{
  if (plugin.GetParam(kParamNoiseEnabled)->Bool()) return SourceMode::Noise;
  return plugin.GetParam(kParamPmonEnabled)->Bool() ? SourceMode::Pmon : SourceMode::Sample;
}

void editParameter(TetoricaSPU& plugin, int parameter, double value)
{
  const double normalized = plugin.GetParam(parameter)->ToNormalized(value);
  plugin.BeginInformHostOfParamChangeFromUI(parameter);
  plugin.SendParameterValueFromUI(parameter, normalized);
  plugin.SendParameterValueFromDelegate(parameter, normalized, true);
  plugin.EndInformHostOfParamChangeFromUI(parameter);
}

void editSourceParameters(TetoricaSPU& plugin, SourceMode mode)
{
  const auto edit = [&](int parameter, bool enabled) {
    if (plugin.GetParam(parameter)->Bool() != enabled)
      editParameter(plugin, parameter, enabled ? 1. : 0.);
  };
  // Noise has priority over the saved PMON flag. Set the winning flag before
  // clearing the other so even an intervening audio block sees a valid mode.
  if (mode == SourceMode::Noise)
  {
    edit(kParamNoiseEnabled, true);
    edit(kParamPmonEnabled, false);
  }
  else
  {
    edit(kParamPmonEnabled, mode == SourceMode::Pmon);
    edit(kParamNoiseEnabled, false);
  }
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

bool sameBaseDefinition(const std::shared_ptr<const tetorica::InstrumentDefinition>& a,
                        const std::shared_ptr<const tetorica::InstrumentDefinition>& b)
{
  if (a == b) return true;
  if (!a || !b || a->kind() != b->kind() ||
      a->program() != b->program() || a->tone() != b->tone()) return false;
  if (a->kind() == tetorica::InstrumentDefinition::Kind::PcmCycle)
    return a->pcmCycle() == b->pcmCycle();
  if (a->kind() == tetorica::InstrumentDefinition::Kind::AdpcmSample)
    return tetorica::sameRawAdpcmSample(a->rawAdpcmSample(), b->rawAdpcmSample());
  return a->bankBytes() == b->bankBytes();
}

bool sameDefinition(const std::shared_ptr<const tetorica::InstrumentDefinition>& a,
                    const std::shared_ptr<const tetorica::InstrumentDefinition>& b)
{
  return sameBaseDefinition(a, b) && (!a ||
    tetorica::sameMorphEndpoints(a->morphEndpoints(), b->morphEndpoints()));
}
const tetorica::PcmCycle* authoredCycle(
  const std::shared_ptr<const tetorica::InstrumentDefinition>& definition)
{
  if(!definition)return nullptr;
  if(definition->kind()==tetorica::InstrumentDefinition::Kind::PcmCycle)
    return &definition->pcmCycle();
  if(definition->kind()==tetorica::InstrumentDefinition::Kind::AdpcmSample&&
     definition->rawAdpcmSample().waveform)return &*definition->rawAdpcmSample().waveform;
  return nullptr;
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
  tetorica::WebInstrumentLoader::Ticket webTicket = 0;
  tetorica::SpuEngineExchange::Generation webBaseGeneration = 0, webCommitted = 0;
  std::uint64_t webRevision = 0;
  std::uint64_t exportTicket = 0, exportRevision = 0;
  tetorica::SpuEngineExchange::Generation exportGeneration = 0;
  std::string exportNotes;
  std::string exportStatus;
  std::uint64_t revision = 0;
  bool baselineInitialized = false;
  bool pickerOpen = false;
  InstrumentPage page = InstrumentPage::Reverb;
  std::optional<SourceMode> displayedSource;
  std::optional<bool> displayedMorph;
  tetorica::PcmDraftState pcmDraft;
  tetorica::MorphDraftState morphDraft;
  bool morphEditB = false;
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
    graphics->AttachControl(new ITextControl(IRECT(24, 18, 420, 58),
      "Tetorica SPU", IText(30, IColor(255, 27, 62, 73))));
    auto* saveWeb=new IVButtonControl(IRECT(432, 22, 550, 54),
      [this](IControl*) { prepareWebExportFromUI(); }, "Save Web...");
    saveWeb->SetTooltip("Save the applied sound as .spu.json for the Web Synth. Current Tone only; Gain and unapplied drafts stay in the DAW. Morph saves its current blend.");
    graphics->AttachControl(saveWeb,kCtrlSaveWeb);
    graphics->AttachControl(new IVButtonControl(IRECT(24, 64, 154, 99),
      [this](IControl*) { promptForVab(); }, "Load sound..."), kCtrlLoadVab);
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
    graphics->AttachControl(new IMultiLineTextControl(IRECT(166, 150, 478, 199),
      mInstrumentUI->status.c_str(), IText(13).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)),
      kCtrlInstrumentStatus);
    const auto smallStyle = DEFAULT_STYLE.WithLabelText(IText(13))
      .WithValueText(IText(14)).WithDrawShadows(false);
    const auto switchStyle = smallStyle.WithShowLabel(false);
    graphics->AttachControl(new IVButtonControl(IRECT(488, 162, 552, 196),
      [this](IControl*) { showInstrumentPage(InstrumentPage::Pcm); }, "Source", smallStyle), kCtrlPagePcm);
    graphics->AttachControl(new IVButtonControl(IRECT(560, 162, 624, 196),
      [this](IControl*) { showInstrumentPage(InstrumentPage::Envelope); }, "ADSR", smallStyle), kCtrlPageEnvelope);
    graphics->AttachControl(new IVButtonControl(IRECT(632, 162, 696, 196),
      [this](IControl*) { showInstrumentPage(InstrumentPage::Reverb); }, "Reverb", smallStyle), kCtrlPageReverb);

    graphics->AttachControl(new ITextControl(IRECT(24, 202, 410, 232),
      "PCM cycle - Software", IText(22).WithAlign(EAlign::Near)), kCtrlPcmHeading);
    auto* sourceMode = new SpuMenuControl(IRECT(432, 202, 696, 232), "Source: Sample",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        constexpr const char* names[] = {"Sample", "SPU Noise", "Sample + PMON"};
        for (int mode = 0; mode < 3; ++mode)
          menu.AddItem(new IPopupMenu::Item(names[mode],
            static_cast<int>(effectiveSourceMode(*this)) == mode ? IPopupMenu::Item::kChecked : 0, mode));
        choice = [this](int mode) {
          if (mode < 0 || mode > 2) return;
          editSourceParameters(*this, static_cast<SourceMode>(mode));
          updateInstrumentControls();
        };
      });
    sourceMode->SetTooltip("Choose Sample, SPU Noise or Sample + PMON. Source changes stop held notes and reverb tails; the sample is kept.");
    graphics->AttachControl(sourceMode, kCtrlSourceMode);
    auto* noiseClock = new IVKnobControl(IRECT(56, 254, 244, 398),
      kParamNoiseClock, "Shared Noise clock", smallStyle, true);
    noiseClock->SetTooltip("Raw SPU Noise clock 0-63, shared by all 24 voices. Changes apply to sounding notes without retriggering.");
    graphics->AttachControl(noiseClock, kCtrlNoiseClock);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(288, 254, 696, 398),
      "One Noise clock is shared by all 24 voices.\n\nKeys play the ADSR envelope. Note and Pitch Bend do not tune Noise.\n\nClock changes apply while notes sound.",
      IText(14).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlNoiseInfo);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(24, 418, 696, 469),
      "SPU Noise uses the retained core's approximate generator.\nYour sample and PCM draft are kept. Switch to Sample to return; switching stops notes and reverb tails.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlNoiseStatus);
    auto* modulator = new SpuMenuControl(IRECT(24, 244, 360, 282), "Modulator: Sine",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        for (int wave = 0; wave < static_cast<int>(kPmonWaveNames.size()); ++wave)
          menu.AddItem(new IPopupMenu::Item(kPmonWaveNames[wave],
            GetParam(kParamPmonWaveform)->Int() == wave ? IPopupMenu::Item::kChecked : 0, wave));
        choice = [this](int wave) {
          if (wave < 0 || wave >= static_cast<int>(kPmonWaveNames.size())) return;
          editParameter(*this, kParamPmonWaveform, wave);
          updateInstrumentControls();
        };
      });
    modulator->SetTooltip("The silent modulator waveform changes the carrier's pitch. It is separate from the retained sample.");
    graphics->AttachControl(modulator, kCtrlPmonWaveform);
    graphics->AttachControl(new IVButtonControl(IRECT(376, 244, 696, 282),
      [this](IControl*) {
        editSourceParameters(*this, SourceMode::Sample);
        updateInstrumentControls();
      }, "Edit carrier (Sample)", smallStyle), kCtrlPmonEditCarrier);
    auto* pmonRatio = new IVKnobControl(IRECT(24, 294, 224, 406),
      kParamPmonRatio, "Ratio", smallStyle, true);
    pmonRatio->SetTooltip("Modulator frequency divided by note frequency, 0.01-16. SPU pitch limits can constrain high ratios.");
    graphics->AttachControl(pmonRatio, kCtrlPmonRatio);
    auto* pmonDepth = new IVKnobControl(IRECT(248, 294, 448, 406),
      kParamPmonDepth, "Depth", smallStyle, true);
    pmonDepth->SetTooltip("Modulation depth. Changes affect sounding notes without retriggering the carrier.");
    graphics->AttachControl(pmonDepth, kCtrlPmonDepth);
    auto* pmonDecay = new IVKnobControl(IRECT(472, 294, 696, 406),
      kParamPmonDecay, "Decay (0 = Hold)", smallStyle, true);
    pmonDecay->SetTooltip("Approximate modulator decay in milliseconds, applied to new notes. Zero holds the modulation; carrier ADSR is independent.");
    graphics->AttachControl(pmonDecay, kCtrlPmonDecay);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(24, 414, 696, 441),
      "2 voices per note: 12 notes. Source changes stop notes and reverb tails.\nThe sample is the carrier. Edit it in Sample, then return to Sample + PMON.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlPmonInfo);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(24, 444, 696, 469),
      "Waveform, Ratio and Depth update sounding notes. Decay applies to new notes.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlPmonStatus);
    auto* morphEnabled = new IVToggleControl(IRECT(24, 238, 188, 268),
      kParamMorphEnabled, "", switchStyle, "Morph: Off", "Morph: On");
    morphEnabled->SetTooltip("Software Morph blends two 112-sample cycles. Off returns to the retained original source. Switching stops notes and reverb tails.");
    graphics->AttachControl(morphEnabled, kCtrlMorphEnabled);
    graphics->AttachControl(new IVButtonControl(IRECT(196, 238, 280, 268),
      [this](IControl*) { selectMorphEndpointFromUI(false); }, "Edit A", smallStyle), kCtrlMorphEditA);
    graphics->AttachControl(new IVButtonControl(IRECT(288, 238, 372, 268),
      [this](IControl*) { selectMorphEndpointFromUI(true); }, "Edit B", smallStyle), kCtrlMorphEditB);
    auto* shapeMenu = new SpuMenuControl(IRECT(388, 238, 696, 268), "Shape: Sine",
      [this](IPopupMenu& menu, SpuMenuControl::Choice& choice) {
        const bool morph = GetParam(kParamMorphEnabled)->Bool();
        const auto& ui = *mInstrumentUI;
        const auto& pcm = morph ? (ui.morphEditB ? ui.morphDraft.endpoints().b :
          ui.morphDraft.endpoints().a) : ui.pcmDraft.cycle();
        for (int shape = 0; shape < 4; ++shape)
          menu.AddItem(new IPopupMenu::Item(kPmonWaveNames[shape],
            pcm == tetorica::makePcmCycle(static_cast<tetorica::WaveformShape>(shape)) ?
              IPopupMenu::Item::kChecked : 0, shape));
        const bool editB = ui.morphEditB;
        const auto revision = ui.revision;
        choice = [this, morph, editB, revision](int shape) {
          if (shape < 0 || shape > 3 || GetParam(kParamMorphEnabled)->Bool() != morph ||
              mInstrumentUI->morphEditB != editB ||
              mInstrumentUIRevision.load(std::memory_order_acquire) != revision) return;
          choosePcmShapeFromUI(static_cast<tetorica::WaveformShape>(shape));
        };
      });
    shapeMenu->SetTooltip("Choose a preset for the editable PCM cycle or the selected Morph endpoint. It remains a draft until Apply.");
    graphics->AttachControl(shapeMenu, kCtrlPcmShape);
    graphics->AttachControl(new SpuWaveformControl(IRECT(24, 278, 696, 392),
      [this](const tetorica::PcmCycle& cycle) { setPcmDraftFromUI(cycle); }), kCtrlPcmGraph);
    auto* revertPcm = new IVButtonControl(IRECT(24, 402, 154, 436),
      [this](IControl*) { revertPcmDraftFromUI(); }, "Revert draft", smallStyle);
    revertPcm->SetTooltip("Discard the current PCM draft or both Morph endpoint drafts. Amount and the original source are kept.");
    graphics->AttachControl(revertPcm, kCtrlPcmRevert);
    auto* applyPcm = new IVButtonControl(IRECT(166, 402, 296, 436),
      [this](IControl*) { applyPcmDraftFromUI(); }, "Apply PCM", smallStyle);
    applyPcm->SetTooltip("Prepare the draft off the audio thread, then replace the sound. Held notes and reverb tails stop.");
    graphics->AttachControl(applyPcm, kCtrlPcmApply);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(308, 399, 696, 440),
      "112 samples -> SPU ADPCM. Draw, then Apply.\nApplying stops notes and reverb tails.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlPcmInfo);
    graphics->AttachControl(new IMultiLineTextControl(IRECT(24, 444, 696, 469),
      "Draft only. Apply PCM to use and save it with the project.",
      IText(12).WithAlign(EAlign::Near).WithVAlign(EVAlign::Top)), kCtrlPcmStatus);
    graphics->AttachControl(new ITextControl(IRECT(308, 398, 696, 414),
      "Morph Amount: A 100% / B 0%", IText(12).WithAlign(EAlign::Near)), kCtrlMorphAmountText);
    auto* morphAmount = new IVSliderControl(IRECT(308, 416, 696, 436),
      kParamMorphAmount, "", smallStyle.WithShowLabel(false).WithShowValue(false), false,
      EDirection::Horizontal);
    morphAmount->SetTooltip("Live blend of the applied endpoints. A/B draft edits are heard only after Apply Morph.");
    graphics->AttachControl(morphAmount, kCtrlMorphAmount);
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
    graphics->AttachControl(new SpuKeyboardControl(IRECT(24, 478, 624, 602), 48, 84),
      kCtrlKeyboard);
    auto* keyboardMorph = new IVSliderControl(IRECT(632, 478, 696, 580),
      kParamMorphAmount, "Morph", smallStyle.WithShowValue(false), false, EDirection::Vertical);
    keyboardMorph->SetTooltip("Software Morph Amount, shared with Source. Live on the applied endpoints; available with Sample or PMON.");
    graphics->AttachControl(keyboardMorph, kCtrlKeyboardMorph);
    graphics->AttachControl(new ITextControl(IRECT(632, 582, 696, 602),
      "Off", IText(12)), kCtrlKeyboardMorphStatus);
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
  mWebLoader.cancel();
  const auto exportState=mWebExporter.status();
  const bool acceptedExport=exportState.state==tetorica::WebInstrumentExporter::State::Writing||
    (!exportState.path.empty()&&(exportState.state==tetorica::WebInstrumentExporter::State::Saved||
      exportState.state==tetorica::WebInstrumentExporter::State::Failed));
  if(!acceptedExport)mWebExporter.cancel();
  if (mInstrumentUI)
  {
    if(mInstrumentUI->webTicket)mInstrumentUI->status="Import cancelled when the editor closed. Previous sound kept.";
    mInstrumentUI->webTicket = 0;
    if(!acceptedExport)
    {
      if(mInstrumentUI->exportTicket)
        mInstrumentUI->status=mInstrumentUI->exportStatus="Web save preparation cancelled when the editor closed.";
      mInstrumentUI->exportTicket = 0;
    }
  }
  if (auto* graphics = GetUI())
  {
    if (auto* graph = graphics->GetControlWithTag(kCtrlEnvelopeGraph))
      static_cast<SpuEnvelopeGraphControl*>(graph)->CancelPointerEdit();
    if (auto* graph = graphics->GetControlWithTag(kCtrlPcmGraph))
      static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  }
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
  mWebExporter.cancel();mInstrumentUI->exportTicket=0;
  mInstrumentUI->pickerOpen = true;
  updateInstrumentControls();
  const std::weak_ptr<InstrumentUIState::Lifetime> lifetime = mInstrumentUI->lifetime;
  const auto revision = mInstrumentUIRevision.load(std::memory_order_acquire);
  const auto generation = mEngines.latestGeneration();
  WDL_String fileName, path;
  GetUI()->PromptForFile(fileName, path, EFileAction::Open, "vab json",
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
            std::string path(file.Get());
            auto extension=path.substr(path.find_last_of('.')==std::string::npos ? path.size() : path.find_last_of('.'));
            std::transform(extension.begin(),extension.end(),extension.begin(),
              [](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(extension==".json")
            {
              ui.webTicket=owner->mWebLoader.requestFile(std::move(path));
              ui.webBaseGeneration=generation;ui.webRevision=revision;
              ui.requested=0;ui.webCommitted=0;
              ui.status="Loading Web instrument. Previous sound remains until ready.";
            }
            else
            {
              owner->mWebLoader.cancel();ui.webTicket=0;ui.webCommitted=0;
              ui.requested = owner->mLoader.requestFile(std::move(path));
              ui.status = "Loading VAB. Previous sound remains until ready.";
            }
          }
        }
        catch (const std::exception& error) { ui.status = std::string("Load failed: ") + error.what(); }
      }
      owner->updateInstrumentControls();
    });
}

void TetoricaSPU::prepareWebExportFromUI()
{
  if(!GetUI()||!mInstrumentUI||mInstrumentUI->pickerOpen||!mInstrumentUI->lifetime)return;
  auto& ui=*mInstrumentUI;
  try
  {
    if(mWebExporter.status().state==tetorica::WebInstrumentExporter::State::Writing)return;
    mWebExporter.cancel();ui.exportTicket=0;ui.exportNotes.clear();
    const auto revision=mInstrumentUIRevision.load(std::memory_order_acquire);
    const auto generation=mEngines.latestGeneration();
    iplug::IByteChunk bytes;
    if(!SerializeState(bytes))throw std::runtime_error("Could not capture a stable sound; try Save Web again");
    auto state=tetorica::decodeState({bytes.GetData(),static_cast<std::size_t>(bytes.Size())});
    std::string name="Builtin Sine";
    switch(state.definition->kind())
    {
      case tetorica::InstrumentDefinition::Kind::Vab:
        name="VAB Program "+std::to_string(state.definition->program())+" Tone "+std::to_string(state.definition->tone());break;
      case tetorica::InstrumentDefinition::Kind::PcmCycle:name="PCM cycle";break;
      case tetorica::InstrumentDefinition::Kind::AdpcmSample:
        name=state.definition->rawAdpcmSample().name;
        if(name.empty())name="Imported sound";
        break;
      case tetorica::InstrumentDefinition::Kind::Builtin:break;
    }
    std::lock_guard<std::mutex> lock(mStateMutex);
    if(revision!=mInstrumentUIRevision.load(std::memory_order_acquire)||
       generation!=mEngines.latestGeneration())
      throw std::runtime_error("Sound changed during capture; try Save Web again");
    ui.exportTicket=mWebExporter.prepare(std::move(state),std::move(name));
    ui.exportRevision=revision;ui.exportGeneration=generation;ui.exportNotes.clear();
    ui.status=ui.exportStatus="Preparing Web sound snapshot...";
  }
  catch(const std::exception& error)
  { ui.status=ui.exportStatus=std::string("Save failed: ")+error.what(); }
  updateInstrumentControls();
}

void TetoricaSPU::promptForWebExport()
{
  using namespace iplug::igraphics;
  if(!GetUI()||!mInstrumentUI||mInstrumentUI->pickerOpen||!mInstrumentUI->lifetime)return;
  auto& ui=*mInstrumentUI;
  const auto ticket=ui.exportTicket;
  const auto revision=ui.exportRevision;
  const auto generation=ui.exportGeneration;
  if(!ticket||!mWebExporter.preview(ticket))return;
  ui.pickerOpen=true;
  updateInstrumentControls();
  const std::weak_ptr<InstrumentUIState::Lifetime> lifetime=ui.lifetime;
  WDL_String fileName("Tetorica-SPU.spu.json"),path;
  GetUI()->PromptForFile(fileName,path,EFileAction::Save,"json",
    [lifetime,ticket,revision,generation](const WDL_String& file,const WDL_String&)
    {
      const auto live=lifetime.lock();if(!live)return;
      std::lock_guard<std::mutex> lifetimeLock(live->mutex);
      auto* owner=live->owner;if(!owner)return;
      auto& ui=*owner->mInstrumentUI;
      ui.pickerOpen=false;
      try
      {
        std::lock_guard<std::mutex> stateLock(owner->mStateMutex);
        if(ui.exportTicket!=ticket||owner->mInstrumentUIRevision.load(std::memory_order_acquire)!=revision||
           owner->mEngines.latestGeneration()!=generation)
        {
          owner->mWebExporter.cancel();ui.exportTicket=0;
          ui.status=ui.exportStatus="Save cancelled because the sound changed.";
        }
        else if(!file.GetLength())
        {
          owner->mWebExporter.cancel();ui.exportTicket=0;
          ui.status=ui.exportStatus="Web save cancelled.";
        }
        else if(owner->mWebExporter.requestSave(ticket,std::string(file.Get())))
          ui.status=ui.exportStatus="Saving Web sound snapshot...";
        else throw std::runtime_error("Prepared Web snapshot is no longer available");
      }
      catch(const std::exception& error)
      {ui.exportTicket=0;ui.status=ui.exportStatus=std::string("Save failed: ")+error.what();}
      owner->updateInstrumentControls();
    });
}

void TetoricaSPU::requestBuiltinFromUI()
{
  try
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mWebLoader.cancel();mInstrumentUI->webTicket=0;mInstrumentUI->webCommitted=0;
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
    mWebLoader.cancel();mInstrumentUI->webTicket=0;mInstrumentUI->webCommitted=0;
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
    mWebLoader.cancel();mInstrumentUI->webTicket=0;mInstrumentUI->webCommitted=0;
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
  bool restored = false;
  bool importedParameters = false;
  bool promptExport = false;
  std::uint64_t revision = 0;
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    revision = mInstrumentUIRevision.load(std::memory_order_acquire);
    restored = ui.revision != revision;
    if (restored)
    {
      ui.revision = revision;
      ui.requested = 0;
      mWebLoader.cancel();ui.webTicket=0;ui.webCommitted=0;
      mWebExporter.cancel();ui.exportTicket=0;
      ui.status = "Project sound restored.";
    }
    if(ui.webTicket)
    {
      if(ui.webRevision!=revision||ui.webBaseGeneration!=mEngines.latestGeneration())
      {
        mWebLoader.cancel();ui.webTicket=0;
      }
      else
      {
        const auto web=mWebLoader.status();
        if(web.ticket==ui.webTicket&&web.state==tetorica::WebInstrumentLoader::State::Failed)
        {
          ui.status="Load failed: "+web.error+". Previous sound kept.";
          ui.webTicket=0;
        }
        else if(auto ready=mWebLoader.takeReady(ui.webTicket))
        {
          // All JSON, sample and native preparation has succeeded on worker.
          // Only now issue a source generation. Failed imports never cancel
          // valid pending source changes, nor modify a single host parameter.
          ready->imported.state.gainPercent=GetParam(kParamGain)->Value();
          try
          {
            if(!std::isfinite(ready->imported.state.gainPercent)||
               ready->imported.state.gainPercent<0||ready->imported.state.gainPercent>100)
              throw std::invalid_argument("Current Gain must be finite 0-100 percent");
            ui.webCommitted=mEngines.commitPreparedImport(std::move(ready->engine),
              std::move(ready->imported.state));
            notify=true;
            ui.requested=0;ui.webTicket=0;
            ui.status="Ready. Waiting for an audio block.";
          }
          catch(const std::exception& error)
          { ui.webTicket=0;ui.status=std::string("Load failed: ")+error.what()+". Previous sound kept."; }
        }
      }
    }
    if(ui.webCommitted)
    {
      if(ui.webCommitted!=mEngines.latestGeneration())ui.webCommitted=0;
      else if(mImportAppliedGeneration.load(std::memory_order_acquire)==ui.webCommitted)
      {
        ui.status="Loaded Web instrument. Play MIDI or the keyboard.";
        importedParameters=true;ui.webCommitted=0;
      }
    }
    const auto status = mLoader.status();
    const auto latest = mEngines.latestGeneration();
    if (ui.requested && ui.requested == latest && status.generation == ui.requested)
    {
      if (status.state == tetorica::VabLoader::State::Ready)
      {
        if(mEngines.adoptedGeneration()==ui.requested)
        {ui.status="Loaded. Play MIDI or the keyboard.";ui.requested=0;}
        else ui.status="Ready. Waiting for an audio block.";
      }
      else if (status.state == tetorica::VabLoader::State::Failed)
      {
        ui.status = std::string("Load failed: ") + status.error + ". Previous sound kept.";
        ui.requested=0;
      }
    }
    else if (ui.requested && ui.requested != latest)
      ui.requested = 0;
    definition = mEngines.snapshotDefinition();
    // Follow the surviving authoring definition, rather than only the latest
    // job status. An adopted successful load still changed state when a newer
    // request fails before the UI had a chance to observe that success.
    if (ui.baselineInitialized && !restored)
      notify = notify || !sameDefinition(ui.notificationBaseline, definition);
    ui.notificationBaseline = definition;
    ui.baselineInitialized = true;
  }
  if(ui.exportTicket)
  {
    const auto status=mWebExporter.status();
    const bool acceptedWrite=status.state==tetorica::WebInstrumentExporter::State::Writing||
      status.state==tetorica::WebInstrumentExporter::State::Saved;
    if(!acceptedWrite&&(ui.exportRevision!=mInstrumentUIRevision.load(std::memory_order_acquire)||
       ui.exportGeneration!=mEngines.latestGeneration()))
    {
      mWebExporter.cancel();ui.exportTicket=0;
      ui.status=ui.exportStatus="Save cancelled because the sound changed.";
    }
    else if(status.ticket==ui.exportTicket)
    {
      if(status.state==tetorica::WebInstrumentExporter::State::Failed)
      {
        ui.status=ui.exportStatus="Save failed: "+status.error;ui.exportTicket=0;
      }
      else if(status.state==tetorica::WebInstrumentExporter::State::Ready)
      {
        ui.exportNotes.clear();
        for(const auto& note:status.notes)ui.exportNotes+=(ui.exportNotes.empty()?"":"\n")+note;
        ui.status=ui.exportStatus="Web snapshot ready. Gain excluded; choose a file.";
        promptExport=!ui.pickerOpen&&GetUI()&&ui.lifetime;
      }
      else if(status.state==tetorica::WebInstrumentExporter::State::Saved)
      {
        ui.status=ui.exportStatus="Saved Web sound. Gain excluded; details in tooltip.";
        ui.exportNotes="File: "+status.path+"\n";
        for(const auto& note:status.notes)ui.exportNotes+=note+"\n";
        ui.exportTicket=0;
      }
    }
  }
  if (definition != ui.definition || restored)
  {
    // A stale stroke must never modify the draft of a restored/replaced sound,
    // even when the restored definition happens to contain identical PCM.
    if (auto* graphics = GetUI())
      if (auto* graph = graphics->GetControlWithTag(kCtrlPcmGraph))
        static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
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
      const auto* acceptedCycle = authoredCycle(definition);
      // Applying endpoint edits replaces the immutable definition, but must
      // not discard an unrelated, unapplied draft of the original PCM source.
      if (restored || !sameBaseDefinition(ui.definition, definition))
        ui.pcmDraft.refresh(acceptedCycle, mEngines.latestGeneration(), restored);
      ui.morphDraft.refresh(definition ? definition->morphEndpoints() : tetorica::MorphEndpoints{},
        mEngines.latestGeneration(), restored);
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
  if(importedParameters)
    for(int parameter=0;parameter<kNumParams;++parameter)
    {
      if(mInstrumentUIRevision.load(std::memory_order_acquire)!=revision)break;
      const double value=GetParam(parameter)->GetNormalized();
      BeginInformHostOfParamChangeFromUI(parameter);
      InformHostOfParamChange(parameter,value);
      SendParameterValueFromDelegate(parameter,value,true);
      EndInformHostOfParamChangeFromUI(parameter);
    }
  // Never call host callbacks with a state/loader mutex held: hosts may save
  // synchronously in response. Do not mark a superseding host restore dirty.
  if (notify && mInstrumentUIRevision.load(std::memory_order_acquire) == revision)
    notifyHostOfInstrumentEdit();
  if(promptExport&&mInstrumentUIRevision.load(std::memory_order_acquire)==revision)
    promptForWebExport();
}

void TetoricaSPU::updateInstrumentControls()
{
  auto* graphics = GetUI();
  if (!graphics || !mInstrumentUI) return;
  auto& ui = *mInstrumentUI;
  const auto mode = effectiveSourceMode(*this);
  const bool noise = mode == SourceMode::Noise;
  const bool pmon = mode == SourceMode::Pmon;
  const bool morph = GetParam(kParamMorphEnabled)->Bool();
  const bool sourceChanged = !ui.displayedSource || *ui.displayedSource != mode;
  const bool morphChanged = !ui.displayedMorph || *ui.displayedMorph != morph;
  if (sourceChanged || morphChanged)
  {
    // Automation and host restore can change source between pointer events.
    // Cancel local editing before hiding its controls; never apply its draft.
    if (auto* graph = graphics->GetControlWithTag(kCtrlPcmGraph))
      static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
    if (auto* graph = graphics->GetControlWithTag(kCtrlEnvelopeGraph))
      static_cast<SpuEnvelopeGraphControl*>(graph)->CancelPointerEdit();
    ui.displayedSource = mode;
    ui.displayedMorph = morph;
  }
  const bool vab = ui.definition && ui.bank &&
    ui.definition->kind() == tetorica::InstrumentDefinition::Kind::Vab;
  if (auto* control = graphics->GetControlWithTag(kCtrlLoadVab))
    if (control->IsDisabled() != ui.pickerOpen) control->SetDisabled(ui.pickerOpen);
  if(auto* control=graphics->GetControlWithTag(kCtrlSaveWeb))
  {
    const bool disabled=ui.pickerOpen||mWebExporter.status().state==tetorica::WebInstrumentExporter::State::Writing;
    if(control->IsDisabled()!=disabled)control->SetDisabled(disabled);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlBankLabel))
  {
    std::string label = vab ?
      (noise ? "Embedded VAB / Noise" : pmon ? "Embedded VAB / PMON" : "Embedded VAB") :
      ui.definition && ui.definition->kind() == tetorica::InstrumentDefinition::Kind::PcmCycle ?
        (noise ? "PCM cycle / Noise" : pmon ? "PCM cycle / PMON" : "Embedded PCM cycle") :
        (noise ? "Builtin Sine / Noise" : pmon ? "Builtin Sine / PMON" : "Builtin Sine");
    if(ui.definition&&ui.definition->kind()==tetorica::InstrumentDefinition::Kind::AdpcmSample)
    {
      label=ui.definition->rawAdpcmSample().name;
      for(auto& character:label)
        if(static_cast<unsigned char>(character)<32||character==127)character=' ';
      if(label.empty())label="Web instrument";
      if(noise)label+=" / Noise";else if(pmon)label+=" / PMON";
    }
    if (morph && !noise) label += " / Morph";
    static_cast<ITextControl*>(control)->SetStr(label.c_str());
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlInstrumentStatus))
  {
    auto* text = static_cast<iplug::igraphics::ITextControl*>(control);
    if (std::strcmp(text->GetStr(), ui.status.c_str()))
    {
      text->SetStr(ui.status.c_str());
      const auto tooltip=ui.status==ui.exportStatus&&!ui.exportNotes.empty()?
        ui.status+"\n"+ui.exportNotes:ui.status;
      control->SetTooltip(tooltip.c_str());
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

  const bool envelopePage = ui.page == InstrumentPage::Envelope;
  const bool reverbPage = ui.page == InstrumentPage::Reverb;
  const bool pcmPage = ui.page == InstrumentPage::Pcm;
  for (int tag = kCtrlEnvelopeGraph; tag <= kCtrlEnvelopeLive; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == envelopePage) control->Hide(!envelopePage);
  for (int tag = kCtrlReverbHeading; tag <= kCtrlReverbStatus; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == reverbPage) control->Hide(!reverbPage);
  const bool pcmEditor = pcmPage && mode == SourceMode::Sample;
  const bool noiseEditor = pcmPage && noise;
  const bool pmonEditor = pcmPage && pmon;
  for (const auto tag : {kCtrlMorphEnabled, kCtrlPcmShape, kCtrlPcmGraph,
                        kCtrlPcmRevert, kCtrlPcmApply, kCtrlPcmStatus})
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == pcmEditor) control->Hide(!pcmEditor);
  for (const auto tag : {kCtrlMorphEditA, kCtrlMorphEditB, kCtrlMorphAmount, kCtrlMorphAmountText})
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == (pcmEditor && morph)) control->Hide(!(pcmEditor && morph));
  if (auto* control = graphics->GetControlWithTag(kCtrlPcmInfo))
    if (control->IsHidden() == (pcmEditor && !morph)) control->Hide(!(pcmEditor && !morph));
  for (int tag = kCtrlNoiseClock; tag <= kCtrlNoiseStatus; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == noiseEditor) control->Hide(!noiseEditor);
  for (int tag = kCtrlPmonWaveform; tag <= kCtrlPmonStatus; ++tag)
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == pmonEditor) control->Hide(!pmonEditor);
  for (const auto tag : {kCtrlPcmHeading, kCtrlSourceMode})
    if (auto* control = graphics->GetControlWithTag(tag))
      if (control->IsHidden() == pcmPage) control->Hide(!pcmPage);
  if (auto* control = graphics->GetControlWithTag(kCtrlPcmHeading))
  {
    const char* title = noise ? "SPU Noise" : pmon ? "Pitch Modulation" :
      morph ? "Morph - Software" : "PCM cycle - Software";
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), title)) text->SetStr(title);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlSourceMode))
  {
    const char* label = noise ? "Source: SPU Noise" : pmon ? "Source: Sample + PMON" : "Source: Sample";
    auto* menu = static_cast<SpuMenuControl*>(control);
    if (std::strcmp(menu->GetLabelStr(), label)) menu->SetLabelStr(label);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlPmonWaveform))
  {
    const auto wave = std::clamp(GetParam(kParamPmonWaveform)->Int(), 0, 3);
    const auto label = std::string("Modulator: ") + kPmonWaveNames[wave];
    auto* menu = static_cast<SpuMenuControl*>(control);
    if (std::strcmp(menu->GetLabelStr(), label.c_str())) menu->SetLabelStr(label.c_str());
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlPmonStatus))
  {
    char message[192];
    const double decay = GetParam(kParamPmonDecay)->Value();
    if (decay == 0)
      std::snprintf(message, sizeof(message), "Waveform / Ratio / Depth are live. Decay: Hold (0), applied to new notes.");
    else
      std::snprintf(message, sizeof(message), "Waveform / Ratio / Depth are live. Decay: approximately %.0f ms, applied to new notes.", decay);
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
  }
  for (const auto tag : {kCtrlPagePcm, kCtrlPageEnvelope, kCtrlPageReverb})
    if (auto* control = graphics->GetControlWithTag(tag))
    {
      const char* label = tag == kCtrlPagePcm ? (pcmPage ? "> Source" : "Source") :
        tag == kCtrlPageReverb ? (reverbPage ? "> Reverb" : "Reverb") :
        (envelopePage ? "> ADSR" : "ADSR");
      auto* button = static_cast<IVButtonControl*>(control);
      if (std::strcmp(button->GetLabelStr(), label)) button->SetLabelStr(label);
    }
  const auto* appliedCycle=authoredCycle(ui.definition);
  const bool appliedPcm=appliedCycle!=nullptr;
  const bool draftApplied=appliedCycle&&*appliedCycle==ui.pcmDraft.cycle();
  const bool endpointsApplied = ui.definition && tetorica::sameMorphEndpoints(
    ui.definition->morphEndpoints(), ui.morphDraft.endpoints());
  const double rawAmount = GetParam(kParamMorphAmount)->Value();
  const double amount = std::isfinite(rawAmount) ? std::clamp(rawAmount, 0., 1.) : 0.;
  if (auto* graph = graphics->GetControlWithTag(kCtrlPcmGraph))
  {
    auto* waveform = static_cast<SpuWaveformControl*>(graph);
    if (morph) waveform->SetMorph(ui.morphDraft.endpoints(), amount, ui.morphEditB, !endpointsApplied);
    else waveform->SetCycle(ui.pcmDraft.cycle());
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlPcmApply))
  {
    const bool disabled = morph ? endpointsApplied : draftApplied;
    if (control->IsDisabled() != disabled) control->SetDisabled(disabled);
    auto* button = static_cast<IVButtonControl*>(control);
    const char* label = morph ? "Apply Morph" : "Apply PCM";
    if (std::strcmp(button->GetLabelStr(), label)) button->SetLabelStr(label);
  }
  for (const auto tag : {kCtrlMorphEditA, kCtrlMorphEditB})
    if (auto* control = graphics->GetControlWithTag(tag))
    {
      const bool selected = ui.morphEditB == (tag == kCtrlMorphEditB);
      const char* label = tag == kCtrlMorphEditA ? (selected ? "> Edit A" : "Edit A") :
        (selected ? "> Edit B" : "Edit B");
      auto* button = static_cast<IVButtonControl*>(control);
      if (std::strcmp(button->GetLabelStr(), label)) button->SetLabelStr(label);
    }
  if (auto* control = graphics->GetControlWithTag(kCtrlPcmShape))
  {
    const auto& pcm = morph ? (ui.morphEditB ? ui.morphDraft.endpoints().b :
      ui.morphDraft.endpoints().a) : ui.pcmDraft.cycle();
    const char* shapeName = "Custom";
    for (int shape = 0; shape < 4; ++shape)
      if (pcm == tetorica::makePcmCycle(static_cast<tetorica::WaveformShape>(shape)))
      { shapeName = kPmonWaveNames[shape]; break; }
    const auto label = std::string(morph ? (ui.morphEditB ? "B shape: " : "A shape: ") : "Shape: ") + shapeName;
    auto* menu = static_cast<SpuMenuControl*>(control);
    if (std::strcmp(menu->GetLabelStr(), label.c_str())) menu->SetLabelStr(label.c_str());
  }
  for (const auto tag : {kCtrlMorphEnabled, kCtrlMorphAmount, kCtrlKeyboardMorph})
    if (auto* control = graphics->GetControlWithTag(tag))
    {
      const bool disabled = noise || (tag != kCtrlMorphEnabled && !morph);
      if (control->IsDisabled() != disabled) control->SetDisabled(disabled);
    }
  if (auto* control = graphics->GetControlWithTag(kCtrlMorphAmountText))
  {
    char message[96];
    std::snprintf(message, sizeof(message), "Live Amount: A %.0f%% / B %.0f%%", (1. - amount) * 100., amount * 100.);
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlKeyboardMorphStatus))
  {
    char message[24];
    if (noise || !morph) std::snprintf(message, sizeof(message), "%s", noise ? "Noise" : "Off");
    else std::snprintf(message, sizeof(message), "%.0f%%", amount * 100.);
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
  }
  if (auto* control = graphics->GetControlWithTag(kCtrlPcmStatus))
  {
    const char* message = morph ? (endpointsApplied ?
      "Applied A/B. Amount is live; Morph Off returns to the retained source. Endpoint Apply stops notes and tails." :
      "Draft A/B preview. Apply Morph to hear/save edits; Amount still controls the applied A/B. Apply stops notes and tails.") : draftApplied ?
      "Applied PCM cycle. The project saves this source PCM and its sound settings." :
      appliedPcm ? "Draft changed. Apply PCM to hear it and save it with the project." :
      vab ? "VAB sound kept. This independent draft replaces it only when you Apply PCM." :
      ui.definition&&ui.definition->kind()==tetorica::InstrumentDefinition::Kind::AdpcmSample ?
      "Web sample kept. This independent draft replaces it only when you Apply PCM." :
      "Builtin Sine kept. Apply PCM creates an editable sound. Unapplied drafts are not saved.";
    auto* text = static_cast<ITextControl*>(control);
    if (std::strcmp(text->GetStr(), message)) text->SetStr(message);
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
    if(ui.definition&&ui.definition->kind()==tetorica::InstrumentDefinition::Kind::AdpcmSample)
      sourceSend=ui.definition->rawAdpcmSample().reverb;
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
  // Like page changes, a source-mode switch overlaps many child rectangles.
  // Invalidate the background once, after all visibility updates are complete.
  if (sourceChanged || morphChanged) graphics->SetAllControlsDirty();
}

void TetoricaSPU::showInstrumentPage(InstrumentPage page)
{
  if (!GetUI() || !mInstrumentUI) return;
  const bool changed = mInstrumentUI->page != page;
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlEnvelopeGraph))
    static_cast<SpuEnvelopeGraphControl*>(graph)->CancelPointerEdit();
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlPcmGraph))
    static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  mInstrumentUI->page = page;
  updateInstrumentControls();
  // Page controls overlap. Hide() invalidates individual child rectangles;
  // repaint the background and new page together after the complete switch.
  if (changed) GetUI()->SetAllControlsDirty();
}

void TetoricaSPU::setPcmDraftFromUI(const tetorica::PcmCycle& cycle)
{
  if (!GetUI() || !mInstrumentUI || effectiveSourceMode(*this) != SourceMode::Sample) return;
  auto& ui = *mInstrumentUI;
  if (GetParam(kParamMorphEnabled)->Bool())
  {
    auto endpoints = ui.morphDraft.endpoints();
    (ui.morphEditB ? endpoints.b : endpoints.a) = cycle;
    ui.morphDraft.set(endpoints);
  }
  else ui.pcmDraft.set(cycle);
  updateInstrumentControls();
}

void TetoricaSPU::selectMorphEndpointFromUI(bool editB)
{
  if (!GetUI() || !mInstrumentUI || effectiveSourceMode(*this) != SourceMode::Sample ||
      !GetParam(kParamMorphEnabled)->Bool() || mInstrumentUI->morphEditB == editB) return;
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlPcmGraph))
    static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  mInstrumentUI->morphEditB = editB;
  updateInstrumentControls();
}

void TetoricaSPU::choosePcmShapeFromUI(tetorica::WaveformShape shape)
{
  if (!GetUI() || !mInstrumentUI || effectiveSourceMode(*this) != SourceMode::Sample) return;
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlPcmGraph))
    static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  setPcmDraftFromUI(tetorica::makePcmCycle(shape));
}

void TetoricaSPU::revertPcmDraftFromUI()
{
  if (!GetUI() || !mInstrumentUI || effectiveSourceMode(*this) != SourceMode::Sample) return;
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlPcmGraph))
    static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  const auto& definition = mInstrumentUI->definition;
  if (GetParam(kParamMorphEnabled)->Bool())
  {
    mInstrumentUI->morphDraft.set(definition ? definition->morphEndpoints() : tetorica::MorphEndpoints{});
    updateInstrumentControls();
  }
  else
  {
    const auto* cycle=authoredCycle(definition);
    setPcmDraftFromUI(cycle ? *cycle : tetorica::makePcmCycle(tetorica::WaveformShape::Sine));
  }
}

void TetoricaSPU::applyPcmDraftFromUI()
{
  if (!GetUI() || !mInstrumentUI || effectiveSourceMode(*this) != SourceMode::Sample) return;
  if (auto* graph = GetUI()->GetControlWithTag(kCtrlPcmGraph))
    static_cast<SpuWaveformControl*>(graph)->CancelPointerEdit();
  try
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    auto& ui = *mInstrumentUI;
    // A host restore can land before the next UI timer refresh. A click on
    // the old draft must not supersede that newly restored project sound.
    if (ui.revision != mInstrumentUIRevision.load(std::memory_order_acquire)) return;
    mWebLoader.cancel();ui.webTicket=0;ui.webCommitted=0;
    if (GetParam(kParamMorphEnabled)->Bool())
    {
      // Capture the latest accepted base, including an earlier Apply that the
      // UI timer has not observed yet. Only this endpoint pair is replaced.
      const auto source = mEngines.snapshotDefinition();
      if (source && tetorica::sameMorphEndpoints(source->morphEndpoints(), ui.morphDraft.endpoints())) return;
      ui.requested = mLoader.requestMorphEndpoints(source, ui.morphDraft.endpoints());
      ui.morphDraft.requested(ui.requested);
      ui.status = "Preparing Morph endpoints. Held notes and reverb tails stop on Apply.";
    }
    else
    {
      const auto* cycle=authoredCycle(ui.definition);
      if(cycle&&*cycle==ui.pcmDraft.cycle())return;
      ui.requested = mLoader.requestPcmCycle(ui.pcmDraft.cycle());
      ui.pcmDraft.requested(ui.requested);
      ui.status = "Preparing PCM. Notes and reverb tails stop when the sound is replaced.";
    }
  }
  catch (const std::exception& error) { mInstrumentUI->status = std::string("Apply failed: ") + error.what(); }
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
  if(ui.definition&&ui.definition->kind()==tetorica::InstrumentDefinition::Kind::AdpcmSample)
    envelope=ui.definition->rawAdpcmSample().envelope;
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
