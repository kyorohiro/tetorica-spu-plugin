// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "WebInstrumentLoader.h"
#include "SpuEngineExchange.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using namespace tetorica;
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
std::string fixture(const char* name) {
  std::ifstream input(std::filesystem::path(WEB_FIXTURE_DIR)/name,std::ios::binary);
  check(bool(input),"fixture missing");
  return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
WebInstrumentLoader::Status finished(WebInstrumentLoader& loader,WebInstrumentLoader::Ticket ticket) {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  for(;;) {
    const auto status=loader.status();
    check(status.ticket==ticket,"wrong ticket");
    if(status.state!=WebInstrumentLoader::State::Loading)return status;
    check(std::chrono::steady_clock::now()<deadline,"worker timeout");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}
}
int main() {
  try {
    SpuEngineExchange exchange;
    auto original=InstrumentDefinition::fromPcmCycle(makePcmCycle(WaveformShape::Triangle));
    auto pending=exchange.commitPrepared(prepareDefinition(*original),original);
    WebInstrumentLoader loader;
    const auto text=fixture("v1-arbitrary.spu.json");
    for(const auto& invalid:{std::string("{broken"),std::string("{}")}) {
      const auto ticket=loader.requestText(invalid);
      const auto status=finished(loader,ticket);
      check(status.state==WebInstrumentLoader::State::Failed&&!status.error.empty(),"invalid import accepted");
      check(!loader.takeReady(ticket),"failed import produced engine");
      check(exchange.latestGeneration()==pending&&exchange.snapshotDefinition()==original,"failed import cancelled pending");
    }
    check(exchange.beginBlock(48000),"valid pending after failures not adopted");
    const auto loopText=fixture("v1-loop-flag4.spu.json");
    auto loopTicket=loader.requestText(loopText);
    check(finished(loader,loopTicket).state==WebInstrumentLoader::State::Ready,"flag4 loop preparation failed");
    auto loop=loader.takeReady(loopTicket);
    const auto expectedLoop=readWebInstrument(loopText);
    check(loop&&sameRawAdpcmSample(loop->imported.state.definition->rawAdpcmSample(),
      expectedLoop.state.definition->rawAdpcmSample()),"Web flag4 source bytes changed during preparation");
    loop->engine->noteOn(60,127);std::vector<float> loopLeft(8192),loopRight(8192);
    loop->engine->render(loopLeft.data(),loopRight.data(),loopLeft.size());
    check(loop->engine->healthy(),"Web flag4 loop rendering failed");
    check(exchange.latestGeneration()==pending,"staging flag4 loop modified exchange");
    auto ticket=loader.requestText(text);
    check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready,"valid import preparation failed");
    check(exchange.latestGeneration()==pending&&exchange.snapshotDefinition()==original,"staging mutated exchange");
    auto ready=loader.takeReady(ticket);
    check(ready&&ready->ticket==ticket&&ready->engine&&ready->engine->healthy(),"prepared ownership missing");
    check(!loader.takeReady(ticket),"prepared result consumed twice");
    ready->imported.state.gainPercent=37;
    const auto imported=exchange.commitPreparedImport(std::move(ready->engine),ready->imported.state);
    check(exchange.snapshotAuthoring().importedState->gainPercent==37,"pending parameter tuple missing");
    check(exchange.beginBlock(48000)&&exchange.adoptedGeneration()==imported,"import adoption failed");
    check(exchange.adoptedImportState()->gainPercent==37,"adopted import lost Gain");

    ticket=loader.requestText(text);
    bool threw=false;
    try {loader.requestText("");} catch(const std::invalid_argument&) {threw=true;}
    check(threw&&loader.status().ticket==ticket,"invalid text cancelled valid staging");
    check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready,"pending valid staging lost");
    loader.cancel();
    check(!loader.takeReady(ticket)&&loader.status().state==WebInstrumentLoader::State::Idle,"cancelled result escaped");

    for(int iteration=0;iteration<12;++iteration) {
      const auto stale=loader.requestText(text);
      ticket=loader.requestText(fixture("v1-morph.spu.json"));
      check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready,"latest request failed");
      check(!loader.takeReady(stale),"obsolete completion escaped");
      auto latest=loader.takeReady(ticket);
      check(latest&&latest->imported.state.morph.enabled,"latest request did not win");
    }
    ticket=loader.requestFile((std::filesystem::path(WEB_FIXTURE_DIR)/"v1-waveform.spu.json").string());
    check(finished(loader,ticket).state==WebInstrumentLoader::State::Ready,"file request failed");
    auto file=loader.takeReady(ticket);
    check(file&&file->imported.state.definition->rawAdpcmSample().waveform.has_value(),"file lost authored waveform");
    const auto unicodeFile=std::filesystem::temp_directory_path()/std::filesystem::u8path(
      "tetorica-日本語-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".spu.json");
    {std::ofstream output(unicodeFile,std::ios::binary);output<<text;check(bool(output),"UTF8 fixture write failed");}
    ticket=loader.requestFile(unicodeFile.u8string());
    const auto unicodeStatus=finished(loader,ticket);
    std::filesystem::remove(unicodeFile);
    check(unicodeStatus.state==WebInstrumentLoader::State::Ready&&loader.takeReady(ticket).has_value(),"UTF8 file path failed");
    ticket=loader.requestFile((std::filesystem::path(WEB_FIXTURE_DIR)/"does-not-exist.spu.json").string());
    check(finished(loader,ticket).state==WebInstrumentLoader::State::Failed,"missing file accepted");
    for(int iteration=0;iteration<8;++iteration) {
      WebInstrumentLoader closing;
      closing.requestText(text);
    }
    std::cout<<"PASS Web worker staging, failed/pending preservation, prepared tuple adoption, latest/cancel, file reads and close/join\n";
  } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
