// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Process-local AU registration and offline PCM/state checks; no device/editor,
// no installation into ~/Library/Audio/Plug-Ins and no external sample path in state.
#include "InstrumentState.h"
#include "vst3-host/midi-controller-checks.h"
#include "vst3-host/envelope-checks.h"
#include "vst3-host/reverb-checks.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message)
{
  if (!condition)
    throw std::runtime_error(message);
}
void ok(OSStatus status, const char* message) { require(status == noErr, message); }

template<class T> class Cf final {
public:
  explicit Cf(T value = nullptr) : mValue(value) {}
  ~Cf() { if (mValue) CFRelease(mValue); }
  Cf(const Cf&) = delete;
  Cf& operator=(const Cf&) = delete;
  Cf(Cf&& other) noexcept : mValue(other.mValue) { other.mValue = nullptr; }
  T get() const { return mValue; }
private:
  T mValue;
};

Cf<CFMutableDictionaryRef> withData(CFPropertyListRef source, const std::vector<std::uint8_t>& bytes)
{
  Cf<CFMutableDictionaryRef> dictionary(CFDictionaryCreateMutableCopy(kCFAllocatorDefault, 0,
    static_cast<CFDictionaryRef>(source)));
  require(dictionary.get(), "Cannot copy AU state dictionary");
  Cf<CFDataRef> data(CFDataCreate(kCFAllocatorDefault, bytes.data(), bytes.size()));
  require(data.get(), "Cannot create AU state data");
  CFDictionarySetValue(dictionary.get(), CFSTR(kAUPresetDataKey), data.get());
  return dictionary;
}

std::vector<std::uint8_t> body(CFPropertyListRef state)
{
  require(state && CFGetTypeID(state) == CFDictionaryGetTypeID(), "Saved AU state is not a dictionary");
  const auto data = CFDictionaryGetValue(static_cast<CFDictionaryRef>(state), CFSTR(kAUPresetDataKey));
  require(data && CFGetTypeID(data) == CFDataGetTypeID(), "Saved AU state lacks CFData");
  const auto ref = static_cast<CFDataRef>(data);
  const auto size = CFDataGetLength(ref);
  require(size > 0 && size <= static_cast<CFIndex>(tetorica::kMaxStateBytes), "Saved AU state is unbounded");
  const auto bytes = CFDataGetBytePtr(ref);
  return {bytes, bytes + size};
}

class Instance final {
public:
  explicit Instance(AudioComponent component)
  {
    ok(AudioComponentInstanceNew(component, &mUnit), "Cannot instantiate local AU");
    try
    {
      AudioStreamBasicDescription format{};
      format.mSampleRate = 48000;
      format.mFormatID = kAudioFormatLinearPCM;
      format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
      format.mBytesPerPacket = 4;
      format.mFramesPerPacket = 1;
      format.mBytesPerFrame = 4;
      format.mChannelsPerFrame = 2;
      format.mBitsPerChannel = 32;
      ok(AudioUnitSetProperty(mUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
        0, &format, sizeof(format)), "AU rejected 48 kHz Float32 stereo");
      UInt32 maximumFrames = 256;
      ok(AudioUnitSetProperty(mUnit, kAudioUnitProperty_MaximumFramesPerSlice,
        kAudioUnitScope_Global, 0, &maximumFrames, sizeof(maximumFrames)), "AU rejected block size");
      ok(AudioUnitInitialize(mUnit), "AU initialization failed");
    }
    catch (...)
    {
      AudioComponentInstanceDispose(mUnit);
      mUnit = nullptr;
      throw;
    }
  }
  ~Instance()
  {
    if (mUnit)
    {
      AudioUnitUninitialize(mUnit);
      AudioComponentInstanceDispose(mUnit);
    }
  }
  Instance(const Instance&) = delete;
  Instance& operator=(const Instance&) = delete;

  Cf<CFPropertyListRef> save() const
  {
    CFPropertyListRef result = nullptr;
    UInt32 size = sizeof(result);
    ok(AudioUnitGetProperty(mUnit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
      0, &result, &size), "AU GetState failed");
    require(result && size == sizeof(result), "AU GetState returned no dictionary");
    return Cf<CFPropertyListRef>(result);
  }
  OSStatus restore(CFPropertyListRef state)
  {
    // The host is deliberately quiescent: no render or MIDI call is in flight.
    return AudioUnitSetProperty(mUnit, kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global,
      0, &state, sizeof(state));
  }
  float gain() const
  {
    AudioUnitParameterValue result = 0;
    ok(AudioUnitGetParameter(mUnit, 0, kAudioUnitScope_Global, 0, &result), "Cannot read AU Gain");
    return result;
  }
  std::vector<std::uint8_t> savedBytes() const {
    const auto state=save();return body(state.get());
  }
  void plainParameterForTest(int index,double plainValue) {
    require(index>=1&&index<=16,"Invalid AU sound parameter ID");
    ok(AudioUnitSetParameter(mUnit,static_cast<AudioUnitParameterID>(index),
      kAudioUnitScope_Global,0,static_cast<AudioUnitParameterValue>(plainValue),0),"AU ADSR parameter rejected");
  }
  std::vector<std::uint8_t> stateBodyForTest() const { return savedBytes(); }
  const std::vector<float>& right() const { return mLastRight; }
  bool tryStateRestoreForTest(const std::vector<std::uint8_t>& bytes) {
    const auto state=save();const auto dictionary=withData(state.get(),bytes);
    return restore(dictionary.get())==noErr;
  }
  void restoreForMidiTest(const std::vector<std::uint8_t>& bytes) {
    const auto state=save();const auto dictionary=withData(state.get(),bytes);
    ok(restore(dictionary.get()),"AU controller test state restore failed");
  }
  void resetForMidiTest() {
    ok(AudioUnitReset(mUnit,kAudioUnitScope_Global,0),"AU controller lifecycle reset failed");
  }
  void reactivateForMidiTest() {
    ok(AudioUnitUninitialize(mUnit),"AU controller deactivation failed");
    ok(AudioUnitInitialize(mUnit),"AU controller reactivation failed");
  }
  void importForMidiTest(const std::vector<std::uint8_t>& bank) {
    restoreForMidiTest(tetorica::encodeState(tetorica::InstrumentDefinition::fromVab(bank,3,0),100));
  }
  std::vector<float> renderMidi(const std::vector<tetorica::test::Midi>& messages,int frames=256) {
    require(frames>0&&frames<=256,"Invalid AU controller test block size");
    for(const auto& message:messages)
      ok(MusicDeviceMIDIEvent(mUnit,message.status,message.data1,message.data2,message.offset),
        "AU controller MIDIEvent rejected");
    const auto audio=render(frames);return {audio.begin(),audio.begin()+frames};
  }
  std::array<float, 256> noteBlock(int offset)
  {
    ok(MusicDeviceMIDIEvent(mUnit, 0x90, 60, 127, offset), "AU MIDI NoteOn rejected");
    return render();
  }
  std::array<float, 256> render(int frames=256)
  {
    std::array<float, 256> left{}, right{};
    struct StereoBuffers { UInt32 count; AudioBuffer channels[2]; } buffers{
      2, {{1, sizeof(left), left.data()}, {1, sizeof(right), right.data()}}};
    AudioTimeStamp timestamp{};
    timestamp.mFlags = kAudioTimeStampSampleTimeValid;
    timestamp.mSampleTime = mPosition;
    AudioUnitRenderActionFlags flags = 0;
    ok(AudioUnitRender(mUnit, &flags, &timestamp, 0, frames,
      reinterpret_cast<AudioBufferList*>(&buffers)), "Offline AU render failed");
    mPosition += frames;
    mLastRight.assign(right.begin(),right.begin()+frames);
    for (int i = 0; i < frames; ++i)
      require(std::isfinite(left[i]) && std::isfinite(right[i]) &&
        std::abs(left[i]) <= 1 && std::abs(right[i]) <= 1, "AU PCM is nonfinite or out of bounds");
    return left;
  }
private:
  AudioUnit mUnit = nullptr;
  double mPosition = 0;
  std::vector<float> mLastRight;
};

void rejected(Instance& instance, CFPropertyListRef bad, const std::vector<std::uint8_t>& expected)
{
  require(instance.restore(bad) != noErr, "Malformed AU state was accepted");
  require(instance.gain() == 37.f, "Malformed AU state changed Gain");
  const auto saved = instance.save();
  require(body(saved.get()) == expected, "Malformed AU state changed the authoring snapshot/pending definition");
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 3)
  {
    std::cerr << "Usage: tetorica_au_state_check /path/TetoricaSPU.component [fixture.vab]\n";
    return 2;
  }
  try
  {
    const auto bundlePath = std::filesystem::absolute(argv[1]).string();
    Cf<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
      reinterpret_cast<const UInt8*>(bundlePath.data()), bundlePath.size(), true));
    require(url.get(), "Cannot create local AU URL");
    Cf<CFBundleRef> bundle(CFBundleCreate(kCFAllocatorDefault, url.get()));
    require(bundle.get() && CFBundleLoadExecutable(bundle.get()), "Cannot load local AU executable");
    const auto factory = reinterpret_cast<AudioComponentFactoryFunction>(
      CFBundleGetFunctionPointerForName(bundle.get(), CFSTR("TetoricaSPU_Factory")));
    require(factory, "Local AU factory symbol missing");
    AudioComponentDescription description{'aumu', 'Tspu', 'Tetr', kAudioComponentFlag_SandboxSafe, 0};
    // This registers the already-loaded factory in this process only. Never
    // copy a plugin into a user's global/per-user Components directory.
    const auto component = AudioComponentRegister(&description, CFSTR("Tetorica: Offline state test"),
      0x00000100, factory);
    require(component, "Cannot register local AU factory");

    const char* fixture = argc == 3 ? argv[2] : VAB_FIXTURE_DIR "/spu-synth-demo.vab";
    std::ifstream input(fixture, std::ios::binary | std::ios::ate);
    require(input.good(), "Cannot open original VAB fixture");
    const auto length = input.tellg();
    require(length > 0 && length <= static_cast<std::streamoff>(tetorica::vab::kMaxFileBytes), "Fixture is oversized");
    std::vector<std::uint8_t> bank(static_cast<std::size_t>(length));
    input.seekg(0);
    require(bool(input.read(reinterpret_cast<char*>(bank.data()), bank.size())), "Cannot read VAB fixture");
    const auto definition = tetorica::InstrumentDefinition::fromVab(bank, 3, 1);
    const auto encoded = tetorica::encodeState(definition, 37);

    Instance instance(component), reference(component);
    const auto initial = instance.save();
    const auto valid = withData(initial.get(), encoded);
    ok(instance.restore(valid.get()), "AU rejected embedded VAB state");
    ok(reference.restore(valid.get()), "Reference AU rejected embedded VAB state");
    require(instance.gain() == 37.f, "AU failed to restore Gain");
    const auto saved = instance.save();
    const auto expected = body(saved.get());
    require(expected == encoded, "AU state does not preserve the embedded original VAB bytes/selection");

    // Reject before the first render as well: a malformed state must retain the
    // already prepared pending VAB instrument, not merely the old active sine.
    for (auto invalid : std::vector<std::vector<std::uint8_t>>{
      {}, {0, 1, 2}, std::vector<std::uint8_t>(39),
      std::vector<std::uint8_t>(tetorica::kMaxStateBytes + 1)})
    {
      const auto state = withData(valid.get(), invalid);
      rejected(instance, state.get(), expected);
    }
    for (std::size_t position : {std::size_t(0), std::size_t(8), std::size_t(12), std::size_t(16), std::size_t(28)})
    {
      auto corrupted = encoded;
      corrupted[position] ^= 0xff;
      const auto state = withData(valid.get(), corrupted);
      rejected(instance, state.get(), expected);
    }
    const auto wrongType = withData(valid.get(), encoded);
    CFDictionarySetValue(wrongType.get(), CFSTR(kAUPresetDataKey), CFSTR("wrong CFData type"));
    rejected(instance, wrongType.get(), expected);
    const auto wrongNumber = withData(valid.get(), encoded);
    CFDictionarySetValue(wrongNumber.get(), CFSTR(kAUPresetTypeKey), CFSTR("wrong CFNumber type"));
    rejected(instance, wrongNumber.get(), expected);
    rejected(instance, CFSTR("not a state dictionary"), expected);

    const auto first = instance.noteBlock(37);
    const auto referenceFirst = reference.noteBlock(37);
    require(first == referenceFirst, "Immediate first post-restore AU render differs from a separate instance");
    require(std::all_of(first.begin(), first.begin() + 37, [](float sample) { return sample == 0; }),
      "Restored AU ignored the sample-offset NoteOn");
    float peak = 0;
    for (int block = 0; block < 32; ++block)
    {
      const auto pcm = instance.render();
      require(pcm == reference.render(), "Embedded VAB AU PCM diverged after state restore");
      for (float sample : pcm) peak = std::max(peak, std::abs(sample));
    }
    require(peak > 0.002f, "Restored VAB AU produced no useful PCM");
    rejected(instance, wrongType.get(), expected); // Active-engine snapshot remains intact too.

    // Legacy AU payload was exactly one binary64 Gain, without iPlug prefix or
    // the VST3-specific four-byte bypass trailer.
    double oldGain = 17.;
    std::uint64_t bits = 0;
    std::memcpy(&bits, &oldGain, sizeof(bits));
    std::vector<std::uint8_t> legacy(8);
    for (unsigned i = 0; i < 8; ++i) legacy[i] = static_cast<std::uint8_t>(bits >> (8 * i));
    const auto legacyState = withData(initial.get(), legacy);
    Instance legacyInstance(component);
    ok(legacyInstance.restore(legacyState.get()), "Legacy AU Gain payload was rejected");
    require(legacyInstance.gain() == 17.f, "Legacy AU Gain was not restored");
    const auto legacySaved = legacyInstance.save();
    const auto legacyBody = body(legacySaved.get());
    const auto decoded = tetorica::decodeState({legacyBody.data(), legacyBody.size()});
    require(decoded.definition->kind() == tetorica::InstrumentDefinition::Kind::Builtin &&
      decoded.gainPercent == 17., "Legacy AU state did not migrate to a builtin definition");
    tetorica::test::checkControllers([&] { return std::make_unique<Instance>(component); },48000,"AU MIDIEvent");
    tetorica::test::checkEnvelopes([&] { return std::make_unique<Instance>(component); },48000,"AU parameters",bank);
    tetorica::test::checkReverbs([&] { return std::make_unique<Instance>(component); },48000,"AU parameters",bank);
    std::cout << "PASS: local AU embedded VAB/Gain round-trip, first-render MIDI/PCM, separate instance, "
      "malformed CF/state rejection with pending/active preservation, legacy AU migration; no audio device/editor/install\n";
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
