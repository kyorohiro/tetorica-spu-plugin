// SPDX-License-Identifier: BSD-3-Clause
// A headless VST3 host: render PCM in memory, never open a device or editor.
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using VST3::Hosting::ClassInfo;
using VST3::Hosting::Module;
using VST3::Hosting::PluginFactory;

namespace {
constexpr int kBlock = 256;
constexpr double kDefaultGain = 1.;
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void ok(tresult result, const char* message) { require(result == kResultOk, message); }
double peak(const std::vector<float>& audio, std::size_t from = 0) {
  double result = 0;
  for (std::size_t n = from; n < audio.size(); ++n)
    result = std::max(result, std::abs(static_cast<double>(audio[n])));
  return result;
}
double rms(const std::vector<float>& audio, std::size_t from) {
  double energy = 0;
  for (std::size_t n = from; n < audio.size(); ++n) energy += audio[n] * audio[n];
  return std::sqrt(energy / (audio.size() - from));
}
Event note(bool on, int offset) {
  Event event{};
  event.busIndex = 0;
  event.sampleOffset = offset;
  event.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
  if (on) {
    event.noteOn.channel = 0; event.noteOn.pitch = 60;
    event.noteOn.velocity = 1.f; event.noteOn.noteId = 101;
  } else {
    event.noteOff.channel = 0; event.noteOff.pitch = 60;
    event.noteOff.velocity = 0.f; event.noteOff.noteId = 101;
  }
  return event;
}

class Instance final {
public:
  Instance(const PluginFactory& factory, const ClassInfo& info, double sampleRate)
    : provider(factory, info), rate(sampleRate) {
    require(provider.initialize(), "PlugProvider initialization failed");
    component = provider.getComponentPtr();
    controller = provider.getControllerPtr();
    processor = component;
    require(component && controller && processor, "Missing VST3 processor/controller");
    bool foundGain = false;
    for (int32 index = 0; index < controller->getParameterCount(); ++index) {
      ParameterInfo parameter{};
      ok(controller->getParameterInfo(index, parameter), "Cannot enumerate parameters");
      const auto* title = parameter.title;
      if (title[0] == 'G' && title[1] == 'a' && title[2] == 'i' &&
          title[3] == 'n' && title[4] == 0) {
        gainId = parameter.id;
        foundGain = true;
        break;
      }
    }
    require(foundGain, "Gain parameter was not found");
    require(component->getBusCount(kAudio, kInput) == 0, "Instrument has unexpected audio input");
    require(component->getBusCount(kAudio, kOutput) == 1, "Expected one output bus");
    require(component->getBusCount(kEvent, kInput) >= 1, "Missing MIDI event input");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    ok(processor->setBusArrangements(nullptr, 0, &stereo, 1), "Stereo arrangement rejected");
    ok(processor->canProcessSampleSize(kSample32), "Float32 processing unsupported");
    ProcessSetup setup{kOffline, kSample32, kBlock, rate};
    ok(processor->setupProcessing(setup), "Offline setupProcessing failed");
    ok(component->activateBus(kAudio, kOutput, 0, true), "Output bus activation failed");
    ok(component->activateBus(kEvent, kInput, 0, true), "Event bus activation failed");
  }
  ~Instance() {
    if (active) {
      processor->setProcessing(false);
      component->setActive(false);
    }
  }
  void start() {
    ok(component->setActive(true), "Component activation failed");
    active = true;
    ok(processor->setProcessing(true), "Processor activation failed");
  }
  void setGain(double value) {
    ok(controller->setParamNormalized(gainId, value), "Controller Gain rejected");
    int32 queueIndex = 0, pointIndex = 0;
    auto* queue = parameters.addParameterData(gainId, queueIndex);
    require(queue, "Cannot enqueue Gain automation");
    ok(queue->addPoint(0, value, pointIndex), "Cannot enqueue Gain value");
  }
  double gain() const { return controller->getParamNormalized(gainId); }
  void save(MemoryStream& state) {
    ok(component->getState(&state), "Processor getState failed");
    require(state.getSize() > 0, "Empty processor state");
  }
  void restore(MemoryStream& state) {
    ok(state.seek(0, IBStream::kIBSeekSet, nullptr), "Cannot rewind state");
    ok(component->setState(&state), "Processor setState failed");
    ok(state.seek(0, IBStream::kIBSeekSet, nullptr), "Cannot rewind controller state");
    ok(controller->setComponentState(&state), "Controller state synchronization failed");
  }
  std::vector<float> render(const Event* event = nullptr) {
    require(active, "Render on inactive instance");
    std::array<float, kBlock> left{}, right{};
    left.fill(std::numeric_limits<float>::quiet_NaN());
    right.fill(std::numeric_limits<float>::quiet_NaN());
    float* channels[]{left.data(), right.data()};
    AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channels;
    EventList events;
    if (event) { auto copy = *event; ok(events.addEvent(copy), "Cannot enqueue note"); }
    ProcessContext context{};
    context.state = ProcessContext::kPlaying;
    context.sampleRate = rate;
    context.projectTimeSamples = position;
    ProcessData data{};
    data.processMode = kOffline;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kBlock;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    data.inputParameterChanges = &parameters;
    data.processContext = &context;
    ok(processor->process(data), "VST3 process failed");
    parameters.clearQueue();
    position += kBlock;
    for (int n = 0; n < kBlock; ++n) {
      require(std::isfinite(left[n]) && std::isfinite(right[n]), "Nonfinite/unwritten PCM output");
      require(std::abs(left[n]) <= 1.f && std::abs(right[n]) <= 1.f, "Unbounded PCM output");
      require(std::abs(left[n] - right[n]) < 1e-7f, "Sine output is unexpectedly asymmetric");
    }
    return {left.begin(), left.end()};
  }
private:
  PlugProvider provider;
  IPtr<IComponent> component;
  IPtr<IEditController> controller;
  FUnknownPtr<IAudioProcessor> processor;
  ParameterChanges parameters{1};
  ParamID gainId{};
  double rate;
  int64 position = 0;
  bool active = false;
};

void append(std::vector<float>& destination, const std::vector<float>& block) {
  destination.insert(destination.end(), block.begin(), block.end());
}

void checkRate(const PluginFactory& factory, const ClassInfo& info, double rate) {
  Instance first(factory, info, rate), independent(factory, info, rate);
  require(std::abs(first.gain() - kDefaultGain) < 1e-6, "Default Gain is not 100 percent");
  first.start(); independent.start();
  require(peak(first.render()) < 1e-12, "Fresh instrument is not silent");
  const auto on = note(true, 64);
  auto sound = first.render(&on);
  for (int n = 0; n < 64; ++n)
    require(std::abs(sound[n]) < 1e-12f, "NoteOn leaked before sample offset 64");
  for (int block = 0; block < 200; ++block) {
    append(sound, first.render());
    require(peak(independent.render()) < 1e-12, "Audio leaked into a second instance");
  }
  require(peak(sound) > 1e-4, "MIDI NoteOn produced no audible PCM");
  int crossings = 0;
  double firstCrossing = 0, lastCrossing = 0;
  const std::size_t from = 4096;
  for (std::size_t n = from + 1; n < sound.size(); ++n) {
    if (sound[n - 1] <= 0 && sound[n] > 0) {
      const double position = n - sound[n] / (sound[n] - sound[n - 1]);
      if (crossings++ == 0) firstCrossing = position;
      lastCrossing = position;
    }
  }
  require(crossings > 2, "Too few C4 cycles to measure");
  const double frequency = (crossings - 1) * rate / (lastCrossing - firstCrossing);
  require(std::abs(frequency - 261.625565) < 1., "Rendered C4 pitch is incorrect");
  const auto off = note(false, 31);
  auto release = first.render(&off);
  for (int block = 0; block < static_cast<int>(2 * rate / kBlock) + 1; ++block)
    release = first.render();
  require(peak(release) < 1e-7, "NoteOff failed to finish its release");

  first.setGain(.37);
  first.render();
  MemoryStream state;
  first.save(state);
  first.setGain(.10);
  first.render();
  require(std::abs(first.gain() - .10) < 1e-6, "Gain did not change before restore");
  first.restore(state);
  require(std::abs(first.gain() - .37) < 1e-6, "Gain state roundtrip failed");
  require(std::abs(independent.gain() - kDefaultGain) < 1e-6, "Gain changed in a second instance");

  Instance restored(factory, info, rate);
  restored.restore(state);
  require(std::abs(restored.gain() - .37) < 1e-6, "New-instance state restore failed");
  restored.start();
  auto baseline = independent.render(&on);
  auto reloaded = restored.render(&on);
  for (int block = 0; block < 120; ++block) {
    append(baseline, independent.render());
    append(reloaded, restored.render());
  }
  const double ratio = rms(reloaded, 4096) / rms(baseline, 4096);
  require(std::isfinite(ratio) && std::abs(ratio - .37 / kDefaultGain) < .01,
    "Restored processor Gain does not match actual PCM amplitude");
  std::cout << "PASS sampleRate=" << rate << " offset=64 peak=" << peak(sound)
    << " c4Hz=" << frequency << " releasePeak=" << peak(release)
    << " restoredGainRatio=" << ratio << " stateBytes=" << state.getSize()
    << " independentInstances=true\n";
}
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "Usage: tetorica_vst3_audio_check /path/to/TetoricaSPU.vst3\n";
    return 2;
  }
  try {
    HostApplication host;
    PluginContextFactory::instance().setPluginContext(&host);
    std::string error;
    const auto path = std::filesystem::canonical(argv[1]).string();
    auto module = Module::create(path, error);
    if (!module) throw std::runtime_error("Cannot load VST3: " + error);
    auto& factory = module->getFactory();
    for (const auto& info : factory.classInfos()) {
      if (info.category() == kVstAudioEffectClass) {
        std::cout << "Loaded " << info.name() << " directly from " << path << '\n';
        for (const double rate : {44100., 48000., 96000.}) checkRate(factory, info, rate);
        PluginContextFactory::instance().setPluginContext(nullptr);
        std::cout << "PASS VST3 offline PCM, timing, release, state and instance isolation\n";
        return 0;
      }
    }
    throw std::runtime_error("No VST3 audio processor class found");
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
