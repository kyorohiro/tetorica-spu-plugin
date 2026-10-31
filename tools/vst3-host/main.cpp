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
#include "InstrumentState.h"
#include "midi-controller-checks.h"
#include "envelope-checks.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
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
    require(!active, "Component already active");
    ok(component->setActive(true), "Component activation failed");
    active = true;
    ok(processor->setProcessing(true), "Processor activation failed");
  }
  void stop() {
    require(active, "Component already inactive");
    ok(processor->setProcessing(false), "Processor deactivation failed");
    ok(component->setActive(false), "Component deactivation failed");
    active = false;
  }
  void restartProcessing() {
    require(active, "Processing restart on inactive component");
    ok(processor->setProcessing(false), "Processor reset failed");
    ok(processor->setProcessing(true), "Processor restart failed");
  }
  void configure(double sampleRate) {
    require(!active, "Reconfigure on active component");
    rate = sampleRate;
    ProcessSetup setup{kOffline, kSample32, kBlock, rate};
    ok(processor->setupProcessing(setup), "Reconfiguration failed");
  }
  void setGain(double value) {
    ok(controller->setParamNormalized(gainId, value), "Controller Gain rejected");
    int32 queueIndex = 0, pointIndex = 0;
    auto* queue = parameters.addParameterData(gainId, queueIndex);
    require(queue, "Cannot enqueue Gain automation");
    ok(queue->addPoint(0, value, pointIndex), "Cannot enqueue Gain value");
  }
  double gain() const { return controller->getParamNormalized(gainId); }
  void allowAsymmetric() { symmetricExpected=false; }
  const std::vector<float>& right() const { return lastRight; }
  std::vector<std::uint8_t> savedBytes() {
    MemoryStream state;save(state);
    const auto* begin=reinterpret_cast<const std::uint8_t*>(state.getData());
    return {begin,begin+state.getSize()};
  }
  bool tryRestore(const std::vector<std::uint8_t>& bytes) {
    MemoryStream state(const_cast<std::uint8_t*>(bytes.data()),bytes.size());
    if(component->setState(&state)!=kResultOk)return false;
    ok(state.seek(0,IBStream::kIBSeekSet,nullptr),"Cannot rewind restored state");
    ok(controller->setComponentState(&state),"Controller restored-state synchronization failed");
    return true;
  }
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
    return renderEvents(event ? std::vector<Event>{*event} : std::vector<Event>{});
  }
  std::vector<float> renderMidi(const std::vector<tetorica::test::Midi>& messages,int frames=kBlock) {
    std::vector<Event> events;
    FUnknownPtr<IMidiMapping> mapping(controller);
    require(bool(mapping),"Actual VST3 lacks IMidiMapping");
    for(const auto& message:messages) {
      const int type=message.status&0xf0,channel=message.status&15;
      if(type==0x90||type==0x80) {
        auto event=note(type==0x90,message.offset);
        if(type==0x90) {
          event.noteOn.channel=channel;event.noteOn.pitch=message.data1;
          event.noteOn.velocity=message.data2/127.f;event.noteOn.noteId=channel*128+message.data1;
        } else {
          event.noteOff.channel=channel;event.noteOff.pitch=message.data1;
          event.noteOff.noteId=channel*128+message.data1;
        }
        events.push_back(event);
      } else if(type==0xb0||type==0xe0) {
        ParamID mapped=0;
        const auto controllerNumber=type==0xe0?kPitchBend:message.data1;
        ok(mapping->getMidiControllerAssignment(0,channel,controllerNumber,mapped),
          "VST3 MIDI controller/channel assignment rejected");
        int32 queueIndex=0,pointIndex=0;
        auto* queue=parameters.addParameterData(mapped,queueIndex);
        require(queue,"Cannot add MIDI-mapped host parameter queue");
        const double value=type==0xe0?((message.data1|(message.data2<<7))/16384.):message.data2/127.;
        ok(queue->addPoint(message.offset,value,pointIndex),"Cannot add MIDI-mapped point");
      } else throw std::runtime_error("Unsupported test MIDI message");
    }
    return renderEvents(events,frames);
  }
  void resetForMidiTest() { restartProcessing();parameters.clearQueue(); }
  void reactivateForMidiTest() { stop();start();parameters.clearQueue(); }
  void importForMidiTest(const std::vector<std::uint8_t>& bank) {
    auto bytes=tetorica::encodeState(tetorica::InstrumentDefinition::fromVab(bank,3,0),100);
    bytes.insert(bytes.end(),4,0);allowAsymmetric();restoreForMidiTest(bytes);
  }
  ParamID mappedControllerForTest(int channel,int controllerNumber) {
    FUnknownPtr<IMidiMapping> mapping(controller);require(bool(mapping),"Missing IMidiMapping");
    ParamID id=0;ok(mapping->getMidiControllerAssignment(0,channel,controllerNumber,id),
      "Cannot map test controller");return id;
  }
  void parameterForTest(ParamID id,double value,int offset) {
    int32 queueIndex=0,pointIndex=0;auto* queue=parameters.addParameterData(id,queueIndex);
    require(queue,"Cannot add adversarial host parameter");
    ok(queue->addPoint(offset,value,pointIndex),"Cannot add adversarial parameter point");
  }
  void envelopeParameterForTest(int index,double plainValue) {
    require(index>=1&&index<=11,"Invalid ADSR test parameter ID");
    const ParamID id=static_cast<ParamID>(index);
    const double normalized=controller->plainParamToNormalized(id,plainValue);
    ok(controller->setParamNormalized(id,normalized),"ADSR controller parameter rejected");
    parameterForTest(id,normalized,0);
  }
  std::vector<std::uint8_t> envelopeBodyForTest() {
    auto bytes=savedBytes();require(bytes.size()>=4,"Missing VST3 state trailer");
    bytes.resize(bytes.size()-4);return bytes;
  }
  bool tryEnvelopeRestoreForTest(std::vector<std::uint8_t> body) {
    body.insert(body.end(),4,0);return tryRestore(body);
  }
  void restoreForMidiTest(const std::vector<std::uint8_t>& bytes) {
    require(tryRestore(bytes),"Controller test state restore failed");parameters.clearQueue();
  }
  std::vector<float> renderEvents(const std::vector<Event>& eventBatch,
                                int frames = kBlock) {
    require(active, "Render on inactive instance");
    require(frames > 0 && frames <= kBlock, "Invalid render block size");
    std::array<float, kBlock> left{}, right{};
    left.fill(std::numeric_limits<float>::quiet_NaN());
    right.fill(std::numeric_limits<float>::quiet_NaN());
    float* channels[]{left.data(), right.data()};
    AudioBusBuffers output{};
    output.numChannels = 2;
    output.channelBuffers32 = channels;
    EventList events(std::max<int32>(128, static_cast<int32>(eventBatch.size())));
    for (auto event : eventBatch) ok(events.addEvent(event), "Cannot enqueue note");
    require(events.getEventCount() == static_cast<int32>(eventBatch.size()),
      "Host EventList dropped test input");
    ProcessContext context{};
    context.state = ProcessContext::kPlaying;
    context.sampleRate = rate;
    context.projectTimeSamples = position;
    ProcessData data{};
    data.processMode = kOffline;
    data.symbolicSampleSize = kSample32;
    data.numSamples = frames;
    data.numOutputs = 1;
    data.outputs = &output;
    data.inputEvents = &events;
    data.inputParameterChanges = &parameters;
    data.processContext = &context;
    ok(processor->process(data), "VST3 process failed");
    parameters.clearQueue();
    position += frames;
    for (int n = 0; n < frames; ++n) {
      require(std::isfinite(left[n]) && std::isfinite(right[n]), "Nonfinite/unwritten PCM output");
      require(std::abs(left[n]) <= 1.f && std::abs(right[n]) <= 1.f, "Unbounded PCM output");
      if(symmetricExpected)require(std::abs(left[n] - right[n]) < 1e-7f, "Sine output is unexpectedly asymmetric");
    }
    lastRight.assign(right.begin(),right.begin()+frames);
    return {left.begin(), left.begin() + frames};
  }
private:
  PlugProvider provider;
  IPtr<IComponent> component;
  IPtr<IEditController> controller;
  FUnknownPtr<IAudioProcessor> processor;
  ParameterChanges parameters{16*kCountCtrlNumber+2};
  ParamID gainId{};
  double rate;
  int64 position = 0;
  bool active = false;
  bool symmetricExpected = true;
  std::vector<float> lastRight;
};

void append(std::vector<float>& destination, const std::vector<float>& block) {
  destination.insert(destination.end(), block.begin(), block.end());
}

double difference(const std::vector<float>& first, const std::vector<float>& second) {
  require(first.size() == second.size(), "Comparison length mismatch");
  double result = 0;
  for (std::size_t n = 0; n < first.size(); ++n)
    result = std::max(result, std::abs(static_cast<double>(first[n]) - second[n]));
  return result;
}

std::vector<float> renderBlocks(Instance& instance, int blocks) {
  std::vector<float> result;
  for (int block = 0; block < blocks; ++block) append(result, instance.render());
  return result;
}

void requireSilent(Instance& instance, const char* message) {
  require(peak(renderBlocks(instance, 8)) < 1e-12, message);
}

void checkScheduler(const PluginFactory& factory, const ClassInfo& info, double rate) {
  // A sample offset equal to numSamples is outside an ordinary VST3 event
  // range. Deliberately exercise our documented deferred-event robustness.
  {
    Instance deferred(factory, info, rate), nextBlock(factory, info, rate);
    deferred.start(); nextBlock.start();
    const auto end = note(true, kBlock);
    const auto zero = note(true, 0);
    require(peak(deferred.render(&end)) < 1e-12, "Block-end note started early");
    require(peak(nextBlock.render()) < 1e-12, "Reference block is not silent");
    auto actual = deferred.render();
    auto expected = nextBlock.render(&zero);
    append(actual, renderBlocks(deferred, 16));
    append(expected, renderBlocks(nextBlock, 16));
    require(peak(actual) > 1e-4, "Deferred block-end note was lost");
    require(difference(actual, expected) < 1e-7,
      "Deferred block-end note differs from next-block sample zero");
  }
  // Equivalent event timelines must give the same PCM across short host blocks.
  {
    Instance whole(factory, info, rate), split(factory, info, rate);
    whole.start(); split.start();
    const auto event = note(true, 64);
    const auto zero = note(true, 0);
    auto actual = whole.render(&event);
    auto expected = split.renderEvents({}, 64);
    append(expected, split.renderEvents({zero}, kBlock - 64));
    append(actual, renderBlocks(whole, 16));
    append(expected, renderBlocks(split, 16));
    require(difference(actual, expected) < 1e-7,
      "MIDI timeline changed with short host-block partitioning");
  }
  {
    Instance offThenOn(factory, info, rate), onThenOff(factory, info, rate);
    offThenOn.start(); onThenOff.start();
    const auto zero = note(true, 0);
    offThenOn.render(&zero); onThenOff.render(&zero);
    renderBlocks(offThenOn, 16); renderBlocks(onThenOff, 16);
    const auto on = note(true, 64), off = note(false, 64);
    auto held = offThenOn.renderEvents({off, on});
    auto released = onThenOff.renderEvents({on, off});
    // Both must preserve the already-playing prefix before the event pair.
    require(difference({held.begin(), held.begin() + 64},
                       {released.begin(), released.begin() + 64}) < 1e-7,
      "Same-offset event pair changed its earlier PCM prefix");
    const int releaseBlocks = static_cast<int>(2 * rate / kBlock) + 1;
    for (int block = 0; block < releaseBlocks; ++block) {
      held = offThenOn.render(); released = onThenOff.render();
    }
    require(peak(held) > 1e-4, "Off then On lost the retriggered held note");
    require(peak(released) < 1e-7, "On then Off did not release the note");
  }
  // EventList preserves insertion order. A normal host supplies sorted offsets;
  // here we also verify safe normalization of deliberately unsorted input.
  {
    Instance unsorted(factory, info, rate), sorted(factory, info, rate);
    unsorted.start(); sorted.start();
    const auto early = note(true, 32), late = note(false, 192);
    auto actual = unsorted.renderEvents({late, early});
    auto expected = sorted.renderEvents({early, late});
    for (int n = 0; n < 32; ++n)
      require(std::abs(actual[n]) < 1e-12f, "Unsorted input leaked before its first offset");
    append(actual, renderBlocks(unsorted, 32));
    append(expected, renderBlocks(sorted, 32));
    require(peak(actual) > 1e-4, "Sorted/unsorted comparison had no audible note");
    require(difference(actual, expected) < 1e-7,
      "Unsorted event list differs from its sorted timeline");
  }
  {
    Instance zeroVelocity(factory, info, rate), explicitOff(factory, info, rate);
    zeroVelocity.start(); explicitOff.start();
    const auto zero = note(true, 0);
    zeroVelocity.render(&zero); explicitOff.render(&zero);
    renderBlocks(zeroVelocity, 16); renderBlocks(explicitOff, 16);
    auto velocityOff = note(true, 32);
    velocityOff.noteOn.velocity = 0;
    const auto off = note(false, 32);
    auto actual = zeroVelocity.render(&velocityOff);
    auto expected = explicitOff.render(&off);
    const int releaseBlocks = static_cast<int>(2 * rate / kBlock) + 1;
    for (int block = 0; block < releaseBlocks; ++block) {
      append(actual, zeroVelocity.render());
      append(expected, explicitOff.render());
    }
    require(difference(actual, expected) < 1e-7,
      "Velocity-zero NoteOn does not behave like NoteOff");
    require(peak(actual, actual.size() - kBlock) < 1e-7,
      "Velocity-zero NoteOn left a note sounding");
  }
  {
    Instance overloaded(factory, info, rate);
    overloaded.start();
    const auto zero = note(true, 0);
    overloaded.render(&zero);
    require(peak(renderBlocks(overloaded, 16)) > 1e-4,
      "Overflow test had no held note to stop");
    // Deliberately exceed the fixed 1024-message queue, ending with a NoteOff
    // that would be dropped by a naive bounded queue and strand a voice.
    std::vector<Event> flood(2049, zero);
    flood.back() = note(false, 0);
    require(peak(overloaded.renderEvents(flood)) < 1e-12,
      "MIDI overflow did not stop all sound at the render boundary");
    requireSilent(overloaded, "MIDI overflow left stale notes sounding");
    const auto recovery = note(true, 64);
    auto recovered = overloaded.render(&recovery);
    for (int n = 0; n < 64; ++n)
      require(std::abs(recovered[n]) < 1e-12f, "Overflow recovery started a note early");
    append(recovered, renderBlocks(overloaded, 16));
    require(peak(recovered) > 1e-4, "Fresh NoteOn did not recover after overflow");
  }
  {
    Instance lifecycle(factory, info, rate);
    lifecycle.start();
    const auto zero = note(true, 0), deferred = note(true, kBlock);
    lifecycle.render(&zero);
    require(peak(renderBlocks(lifecycle, 16)) > 1e-4,
      "Lifecycle test had no held note to clear");
    lifecycle.render(&deferred);
    lifecycle.restartProcessing();
    requireSilent(lifecycle, "Processing restart retained held/deferred notes");
    lifecycle.render(&zero);
    require(peak(renderBlocks(lifecycle, 16)) > 1e-4,
      "Processing restart did not accept a fresh note");
    lifecycle.render(&deferred);
    lifecycle.stop(); lifecycle.start();
    requireSilent(lifecycle, "Deactivate/reactivate retained held/deferred notes");
    lifecycle.render(&zero);
    lifecycle.render(&deferred);
    lifecycle.stop(); lifecycle.configure(rate == 48000 ? 44100 : 48000);
    lifecycle.start();
    requireSilent(lifecycle, "Reconfiguration retained held/deferred notes");
    lifecycle.render(&zero);
    require(peak(renderBlocks(lifecycle, 16)) > 1e-4,
      "Reconfiguration did not accept a fresh note");
  }
  std::cout << "PASS scheduler sampleRate=" << rate
    << " blockEndDeferred=true shortBlocks=true sameOffsetOrder=true"
    << " unsortedOffsets=true velocityZeroOff=true overflow2049Recovered=true"
    << " processingRestartSilent=true reactivateSilent=true reconfigureSilent=true\n";
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
void checkMappedIngress(const PluginFactory& factory,const ClassInfo& info) {
  constexpr double rate=48000;
  auto savedGain=[](Instance& instance) {
    const auto bytes=instance.savedBytes();
    return tetorica::decodeState({bytes.data(),bytes.size()-4}).gainPercent;
  };
  Instance invalid(factory,info,rate),reference(factory,info,rate);
  invalid.start();reference.start();
  invalid.renderMidi({tetorica::test::on()});reference.renderMidi({tetorica::test::on()});
  tetorica::test::equalBlocks(invalid,reference,16,"Invalid-controller warmup diverged");
  invalid.setGain(.37);reference.setGain(.37);
  const auto pitch=invalid.mappedControllerForTest(0,kPitchBend);
  const auto pedal=invalid.mappedControllerForTest(0,kCtrlSustainOnOff);
  int at=0;
  for(const double value:{-.1,1.1,std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()}) {
    invalid.parameterForTest(pitch,value,at);
    invalid.parameterForTest(pedal,value,at++);
  }
  invalid.parameterForTest(pitch,1.,-1);
  invalid.parameterForTest(invalid.mappedControllerForTest(15,kPitchBend)+kCountCtrlNumber,1.,0);
  invalid.parameterForTest(invalid.mappedControllerForTest(0,kCtrlModWheel),1.,0);
  require(difference(invalid.renderMidi({}),reference.renderMidi({}))<1e-7,
    "Invalid/unsupported MIDI-mapped points changed PCM or dropped Gain automation");
  require(savedGain(invalid)==37,
    "Filtered MIDI ingress lost ordinary Gain state");
  require(difference(invalid.renderMidi({tetorica::test::off()}),
    reference.renderMidi({tetorica::test::off()}))<1e-7,"Invalid MIDI points changed NoteOff");
  tetorica::test::equalBlocks(invalid,reference,400,"Invalid MIDI points changed release/pedal state");

  Instance overloaded(factory,info,rate);overloaded.start();
  overloaded.renderMidi({tetorica::test::bend(16383,15),tetorica::test::cc(64,127,15),tetorica::test::on(15)});
  renderBlocks(overloaded,16);
  const auto mapped=overloaded.mappedControllerForTest(15,kPitchBend);
  for(int point=0;point<1025;++point)overloaded.parameterForTest(mapped,.75,point);
  overloaded.setGain(.37);
  require(peak(overloaded.renderMidi({}))<1e-12,"Oversized mapped queue did not invoke safe panic");
  require(savedGain(overloaded)==37,
    "Mapped-point overflow dropped regular Gain automation");
  overloaded.renderMidi({tetorica::test::on(15)});
  require(std::abs(tetorica::test::frequency(tetorica::test::blocks(overloaded,64),rate)-261.625565)<1.,
    "Mapped-point overflow retained live bend");
  overloaded.renderMidi({tetorica::test::off(15)});renderBlocks(overloaded,400);
  require(peak(overloaded.render())<1e-7,"Mapped-point overflow retained sustain");

  // Instruments do not advertise an ordinary bypass parameter in this pinned
  // framework. Exercise its existing reserved bypass-processing path directly;
  // IMidiMapping's CC0 tag follows the reserved bypass/preset tags by two IDs.
  Instance bypassed(factory,info,rate);bypassed.start();
  bypassed.renderMidi({tetorica::test::bend(12288,15),tetorica::test::on(15)});
  require(peak(renderBlocks(bypassed,16))>1e-4,"Bypass test had no held voice");
  const auto bypassId=bypassed.mappedControllerForTest(0,kCtrlBankSelectMSB)-2;
  bypassed.parameterForTest(bypassId,1.,0);bypassed.setGain(.37);
  require(peak(bypassed.renderMidi({tetorica::test::cc(64,127,15,100),
    tetorica::test::bend(16383,15,120),tetorica::test::on(15,60,160)}))<1e-12,
    "Bypass did not mute an already-held voice");
  require(peak(bypassed.renderMidi({tetorica::test::cc(64,0,15,0),tetorica::test::off(15,60,32)}))<1e-12,
    "Bypassed control/note block produced audio");
  const auto bypassState=bypassed.savedBytes();
  require(bypassState[bypassState.size()-4]==1&&savedGain(bypassed)==37,
    "Bypass filtering lost ordinary bypass/Gain forwarding");
  bypassed.parameterForTest(bypassId,0.,0);
  const auto resumed=bypassed.renderMidi({tetorica::test::on(15,60,37)});
  require(peak({resumed.begin(),resumed.begin()+37})<1e-12&&peak(resumed)>1e-4,
    "Unbypass retained old audio/events or discarded its fresh NoteOn");
  require(std::abs(tetorica::test::frequency(tetorica::test::blocks(bypassed,64),rate)-261.625565)<1.,
    "Unbypass replayed old bend/controllers");
  bypassed.renderMidi({tetorica::test::off(15)});renderBlocks(bypassed,400);
  require(peak(bypassed.render())<1e-7,
    "Cross-block bypass pedal events replayed out of time and stranded a voice");
  std::cout<<"PASS VST3 mapped ingress invalidNormalizedIgnored=true invalidChannelIgnored=true"
    <<" unsupportedCCIgnored=true negativeOffsetIgnored=true points1025Panic=true gainPreserved=true"
    <<" bypassMuteQueueReset=true bypassCrossBlockPedal=true freshUnbypassNote=true\n";
}
using StateBytes=std::vector<std::uint8_t>;
StateBytes originalBank() {
  std::ifstream file(std::string(VAB_FIXTURE_DIR)+"/spu-synth-demo.vab",std::ios::binary);
  require(file.good(),"Cannot read original VAB state fixture");
  return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
void put32(StateBytes& bytes,std::size_t offset,std::uint32_t value) {
  for(unsigned n=0;n<4;++n)bytes.at(offset+n)=static_cast<std::uint8_t>(value>>(n*8));
}
void putDouble(StateBytes& bytes,std::size_t offset,double value) {
  std::uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
  for(unsigned n=0;n<8;++n)bytes.at(offset+n)=static_cast<std::uint8_t>(bits>>(n*8));
}
StateBytes vstState(const StateBytes& bank,unsigned program,unsigned tone,double gain=37) {
  auto bytes=tetorica::encodeState(tetorica::InstrumentDefinition::fromVab(bank,program,tone),gain);
  bytes.insert(bytes.end(),4,0); // Official VST3 wrapper's trailing bypass int32.
  return bytes;
}
tetorica::SavedInstrumentState inspectState(const StateBytes& bytes) {
  require(bytes.size()>=44,"Saved VST3 custom state is truncated");
  return tetorica::decodeState({bytes.data(),bytes.size()-4});
}
void restoreRequired(Instance& instance,const StateBytes& state) {
  require(instance.tryRestore(state),"Valid embedded state rejected");
}
void sameStereo(Instance& actual,Instance& reference,const std::vector<Event>& events={}) {
  const auto a=actual.renderEvents(events),b=reference.renderEvents(events);
  require(a==b&&actual.right()==reference.right(),"Embedded-state PCM differs from independent restored instance");
}

void checkEmbeddedState(const PluginFactory& factory,const ClassInfo& info,double rate) {
  const auto bank=originalBank();
  std::vector<float> plain,fine;
  for(unsigned selected=0;selected<4;++selected) {
    auto authored=bank;if(selected==3)authored[0x820+3]=127;
    const unsigned program=selected==2?11:3,toneIndex=selected==1?1:0;
    const auto state=vstState(authored,program,toneIndex);
    Instance first(factory,info,rate),second(factory,info,rate),isolated(factory,info,rate);
    first.allowAsymmetric();second.allowAsymmetric();
    restoreRequired(first,state); // Inactive restoration must prepare before first render.
    require(std::abs(first.gain()-.37)<1e-6,"Embedded Gain was not restored");
    const auto saved=first.savedBytes();
    require(saved==state,"Saved embedded bank/selection/Gain bytes changed before audio adoption");
    const auto decoded=inspectState(saved);
    require(decoded.definition->bankBytes()==authored&&decoded.definition->program()==program&&
      decoded.definition->tone()==toneIndex&&decoded.gainPercent==37,"Saved sparse Tone identity changed");
    restoreRequired(second,saved);
    first.start();second.start();isolated.start();
    const auto on=note(true,0);
    sameStereo(first,second,{on}); // First block immediately following restore.
    std::vector<float> sound=first.right();
    require(peak(sound)>1e-5,"Embedded NoteOn did not sound in the first block");
    if(selected==3)require(peak(first.renderEvents({}))==0,"Imported full-right pan leaked to left");
    if(selected==3)second.render();
    for(int n=0;n<32;++n){sameStereo(first,second);append(sound,first.right());}
    require(peak(isolated.render())==0&&std::abs(isolated.gain()-1)<1e-6,
      "Embedded state affected a separate instance");
    require(first.savedBytes()==state,"Playing changed serialized instrument definition");
    if(selected==0)plain=sound;
    if(selected==1)fine=sound;
    if(selected==2)require(peak(sound,sound.size()-kBlock)==0,"P11 one-shot did not finish");
    if(selected==3)require(peak(first.right())>1e-5,"Imported full-right pan lost right audio");
  }
  require(plain.size()==fine.size()&&difference(plain,fine)>1e-3,
    "Sparse P3 Tone1 fine tuning did not change PCM");

  // Restore between process calls, with held and end-of-block deferred old notes.
  Instance playing(factory,info,rate),fresh(factory,info,rate);
  playing.allowAsymmetric();fresh.allowAsymmetric();playing.start();
  const auto old=note(true,0);playing.render(&old);playing.render();
  auto deferred=note(true,kBlock);deferred.noteOn.pitch=67;playing.render(&deferred);
  auto rightBank=bank;rightBank[0x820+3]=127;const auto rightState=vstState(rightBank,3,0);
  restoreRequired(playing,rightState);restoreRequired(fresh,rightState);fresh.start();
  const auto newNote=note(true,64);
  sameStereo(playing,fresh,{newNote});
  const auto firstAfterRestore=playing.right();
  for(int n=0;n<64;++n)require(firstAfterRestore[n]==0,"Old held/deferred note survived restore");
  require(peak(firstAfterRestore)>1e-5,"Fresh current-block NoteOn was discarded by restore");
  for(int n=0;n<8;++n)sameStereo(playing,fresh);

  // Repeated synchronous restores must not depend on a background collector.
  for(unsigned n=0;n<12;++n) {
    const auto state=n%2?rightState:vstState(bank,3,1);
    Instance reference(factory,info,rate);reference.allowAsymmetric();
    restoreRequired(playing,state);restoreRequired(reference,state);reference.start();
    sameStereo(playing,reference,{old});
    for(int block=0;block<4;++block)sameStereo(playing,reference);
  }
  std::cout<<"PASS embedded state sampleRate="<<rate
    <<" sparseTones=3 stereoPan=true firstBlock=true activeRestore=true heldDeferredCleared=true"
    <<" freshNotePreserved=true repeatedRestores=12 embeddedBytes="<<bank.size()<<'\n';
}

void checkStateFailures(const PluginFactory& factory,const ClassInfo& info) {
  constexpr double rate=48000;
  const auto bank=originalBank(),valid=vstState(bank,3,0);
  Instance target(factory,info,rate),reference(factory,info,rate);
  target.allowAsymmetric();reference.allowAsymmetric();
  restoreRequired(target,valid);restoreRequired(reference,valid);target.start();reference.start();
  const auto on=note(true,0);sameStereo(target,reference,{on});
  std::vector<StateBytes> invalid;
  auto mutate=[&](std::size_t offset,std::uint32_t value){auto state=valid;put32(state,offset,value);invalid.push_back(std::move(state));};
  mutate(0,0);mutate(8,3);mutate(12,1);mutate(16,9);mutate(20,127);mutate(24,99);mutate(28,0xffffffff);
  for(const auto length:{std::size_t(0),std::size_t(1),std::size_t(7),std::size_t(8),std::size_t(12),
      std::size_t(31),std::size_t(39),valid.size()-5,valid.size()-1})
    invalid.emplace_back(valid.begin(),valid.begin()+length);
  for(const double gain:{-1.,101.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
    auto state=valid;putDouble(state,32+bank.size(),gain);invalid.push_back(std::move(state));
  }
  auto oversized=valid;oversized.resize(tetorica::kMaxStateBytes+5);invalid.push_back(std::move(oversized));
  auto trailing=valid;trailing.push_back(0);invalid.push_back(std::move(trailing));
  auto badSample=valid;badSample[32+3616+16]=0x50;invalid.push_back(std::move(badSample));
  auto unsupported=valid;unsupported[32+3616+16+1]&=static_cast<std::uint8_t>(~4);
  unsupported[32+3616+16+17]|=4;invalid.push_back(std::move(unsupported));
  StateBytes badLegacy(12);putDouble(badLegacy,0,std::numeric_limits<double>::quiet_NaN());invalid.push_back(std::move(badLegacy));
  for(std::size_t n=0;n<invalid.size();++n) {
    const auto before=target.savedBytes();const auto gain=target.gain();
    if(target.tryRestore(invalid[n]))throw std::runtime_error("Malformed VST3 state accepted at case "+std::to_string(n));
    require(target.gain()==gain&&target.savedBytes()==before,"Rejected state changed authoring state or Gain");
    sameStereo(target,reference); // Uninterrupted audio, not a fresh retrigger oracle.
  }
  // A failed restore must not invalidate an already prepared pending restore.
  auto rightBank=bank;rightBank[0x820+3]=127;const auto pending=vstState(rightBank,3,0,61);
  restoreRequired(target,pending);
  require(!target.tryRestore(invalid.front()),"Invalid state accepted over a ready restore");
  require(target.savedBytes()==pending&&std::abs(target.gain()-.61)<1e-6,"Failed state cancelled ready state");
  Instance pendingReference(factory,info,rate);pendingReference.allowAsymmetric();
  restoreRequired(pendingReference,pending);pendingReference.start();sameStereo(target,pendingReference,{on});

  StateBytes legacy(12);putDouble(legacy,0,27.5);
  Instance freshLegacy(factory,info,rate),builtin(factory,info,rate);freshLegacy.allowAsymmetric();
  restoreRequired(freshLegacy,legacy);freshLegacy.start();builtin.start();builtin.setGain(.275);
  sameStereo(freshLegacy,builtin,{on});
  require(std::abs(freshLegacy.gain()-.275)<1e-6,"Legacy Gain-only state was not restored");
  // Existing Gain-only projects select Builtin even when replacing an import.
  restoreRequired(target,legacy);
  Instance legacyReference(factory,info,rate);restoreRequired(legacyReference,legacy);legacyReference.start();
  sameStereo(target,legacyReference,{on});
  require(inspectState(target.savedBytes()).definition->kind()==tetorica::InstrumentDefinition::Kind::Builtin,
    "Legacy state retained an imported bank");
  std::cout<<"PASS malformed embedded state cases="<<invalid.size()
    <<" atomicGainStatePCM=true readyRestorePreserved=true legacyGain12=true\n";
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
        for (const double rate : {44100., 48000., 96000.}) {
          checkRate(factory, info, rate);
          checkScheduler(factory, info, rate);
          checkEmbeddedState(factory,info,rate);
          tetorica::test::checkEnvelopes([&] {
            auto instance=std::make_unique<Instance>(factory,info,rate);instance->allowAsymmetric();
            instance->start();return instance;
          },rate,"VST3 parameter changes",originalBank());
          tetorica::test::checkControllers([&] {
            auto instance=std::make_unique<Instance>(factory,info,rate);instance->start();return instance;
          },rate,"VST3 IMidiMapping");
        }
        checkStateFailures(factory,info);
        checkMappedIngress(factory,info);
        PluginContextFactory::instance().setPluginContext(nullptr);
        std::cout << "PASS VST3 offline PCM, scheduler, lifecycle, state and instance isolation\n";
        return 0;
      }
    }
    throw std::runtime_error("No VST3 audio processor class found");
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
