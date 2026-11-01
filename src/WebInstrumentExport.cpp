// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Web v1 layout and compatibility rules follow the original BSD Web reader
// docs/synth-spu/spu_synth_instrument.js. nlohmann JSON is the pinned MIT header.
#include "WebInstrumentExport.h"
#include "WebInstrument.h"
#include "SpuExportSource.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <stdexcept>

namespace tetorica {
namespace {
using Json=nlohmann::json;
[[noreturn]] void fail(const char* reason){throw std::invalid_argument(reason);}
constexpr std::size_t kFreshWebReserved=4096+24*64;
const char* reverbName(ReverbPreset preset) {
  constexpr std::array<const char*,8> names{{"off","room","studio-small","studio-medium","studio-large","hall","space-echo","half-echo"}};
  return names.at(static_cast<std::size_t>(preset));
}
const char* waveformName(WaveformShape shape) {
  constexpr std::array<const char*,4> names{{"sine","triangle","saw","square"}};
  return names.at(static_cast<std::size_t>(shape));
}
}

ExportedWebInstrument writeWebInstrument(const SavedInstrumentState& state,std::string_view name) {
  if(name.empty()||name.size()>320)fail("Web export name must be nonempty and at most 80 UTF-16 units");
  // Validate every authored field before flattening anything. In particular,
  // invalid dormant settings must not disappear behind Noise/Morph priority.
  const auto validated=encodeState(state.definition,state.gainPercent,state.envelope,state.reverb,state.noise,state.pmon,state.morph);
  (void)validated;
  auto source=extractAdpcmSource(*state.definition);
  ExportedWebInstrument exported;
  exported.notes.emplace_back("This is a single Web instrument snapshot, not a complete plugin project. Host bypass, live controllers, sounding notes, effect history and unapplied drafts are not exported.");
  exported.notes.emplace_back("Plugin Gain is not exported or baked into voice volumes. Authored signed volumes are preserved. Web Master and its calibration differ, so playback level can change; a muted plugin can export an audible instrument.");
  if(state.definition->kind()==InstrumentDefinition::Kind::Vab)
    exported.notes.emplace_back("Only the selected VAB Tone is exported, through its first END packet. The full bank, Program/Tone identity, other samples and driver settings are not included.");
  if(state.envelope.enabled) {
    const auto reserved=source.envelope.adsr2&0x2000;
    source.envelope=state.envelope.custom;
    source.envelope.adsr2=static_cast<std::uint16_t>(source.envelope.adsr2|reserved);
    exported.notes.emplace_back("Custom ADSR is baked into the exported raw envelope, preserving the source ADSR2 reserved bit. The original envelope and Custom enable switch are not separately retained.");
  }
  else if(state.envelope.custom.adsr1!=EnvelopeRegisters{}.adsr1||state.envelope.custom.adsr2!=EnvelopeRegisters{}.adsr2)
    exported.notes.emplace_back("Inactive Custom ADSR override values and the Custom switch are not retained. The original source envelope is exported.");
  if(!state.envelope.live)
    exported.notes.emplace_back("The ADSR live/deferred switch is not in the Web format. Export uses the authored next-note envelope, not a currently latched held-note envelope.");
  const bool sourceSend=source.reverb;
  source.reverb=state.reverb.useToneSend?sourceSend:true;
  if(!state.reverb.useToneSend)
    exported.notes.emplace_back("The plugin's All Notes Reverb send mode is baked into the single voice send flag; its separate Tone/All Notes switch is not retained.");
  auto preset=state.reverb.preset;
  if(!state.reverb.enabled) {
    if(preset!=ReverbPreset::Off)
      exported.notes.emplace_back("Disabled Reverb is exported as Off. Return levels are retained, but the dormant selected preset is not retained.");
    preset=ReverbPreset::Off;
  }
  else if(preset==ReverbPreset::Off)
    exported.notes.emplace_back("The Web format has no separate Reverb enable flag. The selected Off preset remains Off.");
  auto pmon=state.pmon;auto morph=state.morph;
  if(state.noise.enabled&&pmon.enabled) {
    pmon.enabled=false;
    exported.notes.emplace_back("Noise takes priority. The masked PMON enable flag is exported Off because the Web format rejects both source flags On; its other PMON settings are retained.");
  }
  if(state.noise.enabled&&morph.enabled) {
    morph.enabled=false;
    exported.notes.emplace_back("Noise takes priority. The masked Morph enable flag is exported Off to retain the original sample; Amount and both endpoints are retained.");
  }
  if(morph.enabled) {
    source.waveform=blendMorph(state.definition->morphEndpoints(),morph.amount);
    const auto encoded=encodePcmCycle(*source.waveform);
    source.adpcm.assign(encoded.begin(),encoded.end());source.baseFrequencyHz=44100./112;source.loopStartSamples=0;
    exported.notes.emplace_back("Active Morph is baked as a canonical ADPCM cycle. The plugin's original base sample/tuning are not included; Web Morph Off keeps the last blend. The plugin's 16-bit carrier and Web 4-bit ADPCM can sound different.");
  } else if(source.waveform) {
    const auto canonical=encodePcmCycle(*source.waveform);
    if(!std::equal(source.adpcm.begin(),source.adpcm.end(),canonical.begin(),canonical.end()))
      exported.notes.emplace_back("Original legacy ADPCM bytes are retained in the file. The Web UI re-encodes authored PCM on loading, so its playback can use canonical bytes instead.");
  }
  if(pmon.enabled)
    exported.notes.emplace_back("PMON settings are retained, but the plugin uses a 16-bit host modulator while the Web synthesizer uses encoded ADPCM. Their modulation is not guaranteed sample-identical.");
  if(!source.waveform) {
    const auto capacity=524288-kFreshWebReserved-(pmon.enabled?64:0);
    if(source.adpcm.size()>capacity)
      fail("Exported ADPCM cannot fit a fresh Web synthesizer: reserved editor slots and optional PMON need additional sample RAM");
    exported.notes.emplace_back("Loading this ADPCM consumes Web sample RAM. Reload the Web synthesizer for a fresh bank if earlier samples leave insufficient space.");
  }
  validateRawAdpcmSample(source);
  Json sample{{"adpcm",source.adpcm},{"baseFrequencyHz",source.baseFrequencyHz}};
  if(source.loopStartSamples)sample["loopStartSamples"]=*source.loopStartSamples;
  Json value{{"schema","tetorica-spu-synth.instrument"},{"version",1},{"name",std::string(name)},
    {"preset",{{"volumeLeft",source.volumeLeft},{"volumeRight",source.volumeRight},
      {"adsr1",source.envelope.adsr1},{"adsr2",source.envelope.adsr2},{"pitch",source.inspectorPitch},{"reverb",source.reverb}}},
    {"sample",std::move(sample)},
    {"reverb",{{"preset",reverbName(preset)},{"volumeLeft",state.reverb.volumeLeft},{"volumeRight",state.reverb.volumeRight}}},
    {"waveform",source.waveform?Json(*source.waveform):Json(nullptr)},
    {"noise",{{"enabled",state.noise.enabled},{"clock",state.noise.clock}}},
    {"pmon",{{"enabled",pmon.enabled},{"waveform",waveformName(pmon.waveform)},{"ratio",pmon.ratio},{"depth",pmon.depth},{"decayMs",pmon.decayMs}}},
    {"morph",{{"enabled",morph.enabled},{"amount",morph.amount},{"a",state.definition->morphEndpoints().a},{"b",state.definition->morphEndpoints().b}}}};
  exported.json=value.dump(2)+"\n";
  if(exported.json.size()>kMaxWebInstrumentBytes)fail("Exported Web instrument exceeds the 8MiB JSON bound");
  // The same strict importer validates the complete final representation. Name
  // trimming follows the real Web reader rather than platform locale rules.
  const auto checked=readWebInstrument(exported.json);
  if(checked.name!=name) {
    value["name"]=checked.name;exported.json=value.dump(2)+"\n";
    if(exported.json.size()>kMaxWebInstrumentBytes)fail("Exported Web instrument exceeds the 8MiB JSON bound");
  }
  return exported;
}
} // namespace tetorica
