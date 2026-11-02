// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "../plugin/PcmApplyState.h"
#include "../plugin/PcmDraftState.h"
#include "../plugin/MorphDraftState.h"
#include "../plugin/SoundPreparationStatus.h"
#include "VabLoader.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

tetorica::VabLoader::Status awaitPreparation(tetorica::VabLoader& loader,
    tetorica::VabLoader::Generation generation) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto status = loader.status();
    require(status.generation == generation, "Unexpected worker generation");
    if (status.state != tetorica::VabLoader::State::Loading) return status;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  throw std::runtime_error("Sound preparation timed out");
}

void stoppedHostPreparation() {
  using namespace tetorica;
  using namespace tetorica::ui;
  SpuEngineExchange exchange;
  VabLoader loader(exchange);
  const auto triangle = makePcmCycle(WaveformShape::Triangle);
  const auto pcm = loader.requestPcmCycle(triangle);
  auto status = awaitPreparation(loader, pcm);
  require(status.state == VabLoader::State::Ready, "PCM worker failed");
  PcmApplyState apply;
  apply.requested(pcm, triangle);
  const auto phase = [&] {
    const auto s = loader.status();
    return soundPreparationPhase(s.generation, exchange.latestGeneration(), exchange.adoptedGeneration(),
      s.state == VabLoader::State::Loading, s.state == VabLoader::State::Ready);
  };
  // Exactly the GarageBand case: publication has completed, but the host has
  // not called the audio callback. UI polling alone must never say Loaded.
  for (int poll = 0; poll < 4; ++poll) {
    require(phase() == SoundPreparationPhase::Ready, "A stopped host was mislabeled Preparing/Loaded");
    require(!std::strcmp(soundPreparationButtonLabel(phase()), "Ready"), "Ready button is misleading");
    require(!apply.requestNeeded(triangle, true, pcm, true), "Ready required another Apply request");
    require(exchange.adoptedGeneration() != pcm, "UI observation adopted the engine");
  }
  require(exchange.beginBlock(48000), "First audio block did not adopt PCM");
  require(phase() == SoundPreparationPhase::Loaded, "First audio block did not complete the status");

  const auto morph = loader.requestMorphEndpoints(exchange.snapshotDefinition(),
    {triangle, makePcmCycle(WaveformShape::Square)});
  status = awaitPreparation(loader, morph);
  require(status.state == VabLoader::State::Ready && phase() == SoundPreparationPhase::Ready,
    "Morph endpoints did not wait for audio");
  const auto newer = loader.requestBuiltin();
  require(soundPreparationPhase(morph, exchange.latestGeneration(), exchange.adoptedGeneration(), false, true)
    == SoundPreparationPhase::Idle, "Superseded Morph left a stale Ready status");
  status = awaitPreparation(loader, newer);
  require(status.state == VabLoader::State::Ready && exchange.beginBlock(48000), "New source did not adopt");
  require(phase() == SoundPreparationPhase::Loaded, "New source stayed Ready after adoption");

  const auto failed = loader.request({1, 2, 3}, 0);
  status = awaitPreparation(loader, failed);
  require(status.state == VabLoader::State::Failed && phase() == SoundPreparationPhase::Idle,
    "A preparation failure left a stale Ready status");
  const auto definition = InstrumentDefinition::builtinWide();
  exchange.commitPrepared(prepareDefinition(*definition), definition);
  require(phase() == SoundPreparationPhase::Idle, "Restore retained an old worker status");

  SavedInstrumentState imported; imported.definition = definition;
  const auto importGeneration = exchange.commitPreparedImport(prepareDefinition(*definition), imported);
  require(soundPreparationPhase(importGeneration, exchange.latestGeneration(), 0, false, true)
    == SoundPreparationPhase::Ready, "Prepared import skipped the wait for audio");
  require(exchange.beginBlock(48000), "Import engine did not adopt");
  require(soundPreparationPhase(importGeneration, exchange.latestGeneration(), 0, false, true)
    == SoundPreparationPhase::Ready, "Engine-only adoption falsely completed the import tuple");
  require(soundPreparationPhase(importGeneration, exchange.latestGeneration(), importGeneration, false, true)
    == SoundPreparationPhase::Loaded, "Applied import tuple stayed Ready");
}
}

int main() {
  try {
    using namespace tetorica;
    using tetorica::ui::PcmApplyState;
    using Target = PcmApplyState::Target;
    const auto a = makePcmCycle(WaveformShape::Sine);
    const auto b = makePcmCycle(WaveformShape::Triangle);
    const auto c = makePcmCycle(WaveformShape::Square);
    const MorphEndpoints endpointsA{a, b}, endpointsB{b, c};
    PcmApplyState apply;
    require(apply.enabled() && !apply.autoApply(Target::Pcm) &&
      !apply.autoApply(Target::Morph), "Opening an editor would replace its untouched source");
    apply.edited(Target::Pcm);
    require(apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph),
      "Completed PCM edit did not request default automatic apply");
    apply.requested(1, b);
    require(!apply.requestNeeded(b, false, 1, true), "Duplicate pending PCM restarted preparation");
    require(apply.requestNeeded(a, true, 1, true), "A -> B -> A did not supersede pending B");
    apply.requested(2, a);
    require(!apply.requestNeeded(a, true, 2, true), "Repeated return to A restarted preparation");
    require(apply.requestNeeded(c, false, 2, true), "Newer PCM edit was ignored");
    require(apply.requestNeeded(a, true, 3, true), "External pending source was mistaken for our request");
    require(!apply.requestNeeded(a, true, 2, false), "An applied cycle restarted without a pending change");
    require(apply.requestNeeded(b, false, 1, false), "Failed request could not be explicitly retried");

    apply.setEnabled(false);
    apply.edited(Target::Pcm); apply.edited(Target::Morph);
    require(!apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph), "Manual drafts still auto-applied");
    apply.setEnabled(true);
    require(apply.autoApply(Target::Pcm) && apply.autoApply(Target::Morph), "Re-enabling lost intentional manual edits");
    apply.synchronized(Target::Pcm);
    require(!apply.autoApply(Target::Pcm) && apply.autoApply(Target::Morph), "Accepting PCM erased a separate Morph draft");
    apply.requested(4, endpointsA);
    require(!apply.requestNeeded(endpointsA, false, 4, true), "Duplicate endpoint Apply restarted preparation");
    require(apply.requestNeeded(endpointsB, false, 4, true), "Newer endpoint pair was ignored");
    require(apply.requestNeeded(a, true, 4, true), "PCM choice failed to supersede pending endpoints");
    apply.restored();
    require(apply.enabled() && !apply.autoApply(Target::Pcm) && !apply.autoApply(Target::Morph) &&
      !apply.sameRequest(endpointsA, 4), "Restore retained stale edit intent or request ownership");
    apply.setEnabled(false); apply.restored();
    require(!apply.enabled(), "Restore changed the editor's Auto apply preference");

    // Auto apply uses the same draft ownership as manual Apply. Intermediate
    // worker observations must never erase a more recent completed stroke.
    PcmDraftState pcm;
    pcm.set(b); pcm.requested(5); pcm.set(c); pcm.requested(6);
    pcm.refresh(&b, 6, false);
    require(pcm.cycle() == c, "Intermediate automatic PCM erased the latest edit");
    MorphDraftState morph;
    morph.set(endpointsA); morph.requested(7); morph.set(endpointsB); morph.requested(8);
    morph.refresh(endpointsA, 8, false);
    require(sameMorphEndpoints(morph.endpoints(), endpointsB), "Intermediate automatic endpoints erased the latest edit");
    stoppedHostPreparation();
    std::cout << "PASS PCM apply defaultOn=true untouchedSource=true duplicateSuppression=true returnToActive=true manualDraft=true explicitRetry=true restore=true newestEdit=true stoppedHostReady=true firstBlockLoaded=true staleStatusCleared=true importTupleMarker=true\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL PCM apply: " << error.what() << '\n'; return 1;
  }
}
