// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Process-local AU registration and offline PCM/state checks; no device/editor,
// no installation into ~/Library/Audio/Plug-Ins and no external sample path in state.
#include "InstrumentState.h"
#include "vst3-host/midi-controller-checks.h"
#include "vst3-host/envelope-checks.h"
#include "vst3-host/reverb-checks.h"
#include "vst3-host/pcm-cycle-checks.h"
#include "vst3-host/morph-checks.h"
#include "vst3-host/web-instrument-checks.h"
#include "vst3-host/wav-import-checks.h"
#include "vst3-host/software-audio-checks.h"
#include "vst3-host/loop-repeat-checks.h"
#include "vst3-host/gain-smoothing-checks.h"
#include "vst3-host/cc120-checks.h"
#include "vst3-host/builtin-wide-checks.h"
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

CFStringRef stateName(CFPropertyListRef state)
{
  require(state && CFGetTypeID(state) == CFDictionaryGetTypeID(), "Named AU state is not a dictionary");
  const auto name = CFDictionaryGetValue(static_cast<CFDictionaryRef>(state), CFSTR(kAUPresetNameKey));
  require(name && CFGetTypeID(name) == CFStringGetTypeID(), "Saved AU name is not a CFString");
  return static_cast<CFStringRef>(name); // Borrowed from the caller-owned state.
}

class Instance final {
public:
  explicit Instance(AudioComponent component,double sampleRate=48000)
  {
    ok(AudioComponentInstanceNew(component, &mUnit), "Cannot instantiate local AU");
    try
    {
      AudioStreamBasicDescription format{};
      format.mSampleRate = sampleRate;
      format.mFormatID = kAudioFormatLinearPCM;
      format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
      format.mBytesPerPacket = 4;
      format.mFramesPerPacket = 1;
      format.mBytesPerFrame = 4;
      format.mChannelsPerFrame = 2;
      format.mBitsPerChannel = 32;
      ok(AudioUnitSetProperty(mUnit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output,
        0, &format, sizeof(format)), "AU rejected requested-rate Float32 stereo");
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
  Cf<CFArrayRef> factoryPresets() const
  {
    CFArrayRef presets = nullptr;
    UInt32 size = sizeof(presets);
    ok(AudioUnitGetProperty(mUnit, kAudioUnitProperty_FactoryPresets, kAudioUnitScope_Global,
      0, &presets, &size), "AU factory-presets property rejected");
    require(presets && size == sizeof(presets) && CFGetTypeID(presets) == CFArrayGetTypeID(),
      "AU factory-presets property is not an owned CFArray");
    return Cf<CFArrayRef>(presets);
  }
  AUPreset presentPreset(AudioUnitPropertyID property = kAudioUnitProperty_PresentPreset) const
  {
    AUPreset preset{-1, nullptr};
    UInt32 size = sizeof(preset);
    ok(AudioUnitGetProperty(mUnit, property, kAudioUnitScope_Global,
      0, &preset, &size), "AU present-preset property rejected");
    require(size == sizeof(preset) && preset.presetName &&
      CFGetTypeID(preset.presetName) == CFStringGetTypeID(), "AU present preset lacks its owned name");
    return preset; // PresentPreset transfers +1 ownership of presetName.
  }
  void restoreFactoryPreset(int number)
  {
    const AUPreset preset{number, CFSTR("The factory number selects its sound")};
    ok(AudioUnitSetProperty(mUnit, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global,
      0, &preset, sizeof(preset)), "AU factory preset recall rejected");
  }
  OSStatus setPresentPreset(int number, CFStringRef name)
  {
    const AUPreset preset{number, name};
    return AudioUnitSetProperty(mUnit, kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global,
      0, &preset, sizeof(preset));
  }
  OSStatus setPresetProperty(AudioUnitPropertyID property, AudioUnitScope scope,
    AudioUnitElement element, const void* value, UInt32 bytes)
  {
    return AudioUnitSetProperty(mUnit, property, scope, element, value, bytes);
  }
  OSStatus getPresetProperty(AudioUnitPropertyID property, AudioUnitScope scope,
    AudioUnitElement element, void* value, UInt32& bytes) const
  {
    return AudioUnitGetProperty(mUnit, property, scope, element, value, &bytes);
  }
  float gain() const
  {
    AudioUnitParameterValue result = 0;
    ok(AudioUnitGetParameter(mUnit, 0, kAudioUnitScope_Global, 0, &result), "Cannot read AU Gain");
    return result;
  }
  std::array<double,26> soundParametersForBuiltinTest() const
  {
    UInt32 bytes=0;Boolean writable=false;
    ok(AudioUnitGetPropertyInfo(mUnit,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,
      0,&bytes,&writable),"Cannot inspect AU sound-parameter list");
    require(bytes==26*sizeof(AudioUnitParameterID),"AU changed the 26 public sound parameters");
    std::array<AudioUnitParameterID,26> ids{};
    ok(AudioUnitGetProperty(mUnit,kAudioUnitProperty_ParameterList,kAudioUnitScope_Global,
      0,ids.data(),&bytes),"Cannot read AU sound-parameter list");
    require(bytes==sizeof(ids),"AU returned a truncated sound-parameter list");
    std::sort(ids.begin(),ids.end());std::array<double,26> values{};
    for(unsigned i=0;i<values.size();++i)
    {
      require(ids[i]==i,"AU public sound-parameter IDs changed");
      AudioUnitParameterValue value=0;
      ok(AudioUnitGetParameter(mUnit,ids[i],kAudioUnitScope_Global,0,&value),
        "Cannot read AU sound parameter");
      values[i]=value;
    }
    return values;
  }
  void setGainForTest(double normalized) {
    require(std::isfinite(normalized)&&normalized>=0&&normalized<=1,"Invalid test Gain");
    ok(AudioUnitSetParameter(mUnit,0,kAudioUnitScope_Global,0,
      static_cast<AudioUnitParameterValue>(normalized*100),0),"AU Gain parameter rejected");
  }
  void setBypassForGainTest(bool bypassed) {
    const UInt32 value=bypassed?1:0;
    ok(AudioUnitSetProperty(mUnit,kAudioUnitProperty_BypassEffect,
      kAudioUnitScope_Global,0,&value,sizeof(value)),"AU Gain-test bypass rejected");
  }
  std::vector<std::uint8_t> savedBytes() const {
    const auto state=save();return body(state.get());
  }
  void plainParameterForTest(int index,double plainValue) {
    require(index>=1&&index<=25,"Invalid AU sound parameter ID");
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

void checkPresetNames(AudioComponent component, const std::vector<std::uint8_t>& bank)
{
  Instance named(component), reference(component);
  const auto initial = named.save();
  const auto defaultBody = body(initial.get());
  require(tetorica::decodeState({defaultBody.data(),defaultBody.size()}).definition->kind()==
    tetorica::InstrumentDefinition::Kind::BuiltinSineWide,"AU factory default is not full-range builtin Sine");
  const auto present = named.presentPreset();
  const Cf<CFStringRef> presentName(present.presetName);
  const auto factories = named.factoryPresets();
  require(CFArrayGetCount(factories.get()) == 1, "AU must publish one initialized builtin factory preset");
  const auto factory = static_cast<const AUPreset*>(CFArrayGetValueAtIndex(factories.get(), 0));
  require(factory && factory->presetNumber == 0 && factory->presetName &&
    CFEqual(factory->presetName, CFSTR("TetoricaSPU")) && present.presetNumber == 0 &&
    CFEqual(presentName.get(), factory->presetName) && CFEqual(stateName(initial.get()), presentName.get()),
    "AU FactoryPresets/PresentPreset/ClassInfo default names disagree");
  // AU hosts also assign a descriptive user name through PresentPreset, with
  // a negative number. This must rename metadata without recalling a sound.
  ok(named.setPresentPreset(-1, CFSTR("User name from PresentPreset")), "AU rejected a user preset name");
  const auto userPropertyState = named.save();
  require(CFEqual(stateName(userPropertyState.get()), CFSTR("User name from PresentPreset")) &&
    body(userPropertyState.get()) == defaultBody && named.gain() == 100.f,
    "AU PresentPreset user name was not retained in retrieved ClassInfo data");
  const auto userProperty = named.presentPreset();
  const Cf<CFStringRef> userPropertyName(userProperty.presetName);
  require(userProperty.presetNumber == -1 && CFEqual(userPropertyName.get(), stateName(userPropertyState.get())),
    "AU PresentPreset did not retain its user number/name");
  named.restoreFactoryPreset(0);

  const auto encoded = tetorica::encodeState(tetorica::InstrumentDefinition::fromVab(bank, 3, 1), 37);
  const Cf<CFStringRef> unicode(CFStringCreateWithCString(kCFAllocatorDefault,
    "User \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x8e\xb9", kCFStringEncodingUTF8));
  const auto maximumText = std::string(1024, 'x');
  const Cf<CFStringRef> maximumName(CFStringCreateWithCString(kCFAllocatorDefault,
    maximumText.c_str(), kCFStringEncodingUTF8));
  require(unicode.get() && maximumName.get(), "Cannot create AU metadata test names");
  // A name equal to the factory entry is still descriptive: the embedded VAB
  // and Gain must win, rather than recalling builtin state by string lookup.
  for(const auto name : {CFSTR("TetoricaSPU"), unicode.get(), CFSTR(""), maximumName.get()})
  {
    const auto state = withData(initial.get(), encoded);
    CFDictionarySetValue(state.get(), CFSTR(kAUPresetNameKey), name);
    ok(named.restore(state.get()), "AU rejected a valid bounded ClassInfo name");
    const auto saved = named.save();
    require(CFEqual(stateName(saved.get()), name) && body(saved.get()) == encoded && named.gain() == 37.f,
      "AU failed to retain a ClassInfo name or recalled a preset by name");
  }
  const Cf<CFMutableStringRef> mutableName(CFStringCreateMutableCopy(kCFAllocatorDefault, 0, unicode.get()));
  require(mutableName.get(), "Cannot create mutable AU name");
  const auto mutableState = withData(initial.get(), encoded);
  CFDictionarySetValue(mutableState.get(), CFSTR(kAUPresetNameKey), mutableName.get());
  ok(named.restore(mutableState.get()), "AU rejected mutable ClassInfo metadata");
  CFStringReplaceAll(mutableName.get(), CFSTR("Host changed its original mutable string"));
  require(CFEqual(stateName(named.save().get()), unicode.get()), "AU borrowed a mutable host name");
  const auto referenceState = withData(initial.get(), encoded);
  CFDictionarySetValue(referenceState.get(), CFSTR(kAUPresetNameKey), CFSTR("Independent instance"));
  ok(reference.restore(referenceState.get()), "AU reference name restore failed");
  require(CFEqual(stateName(named.save().get()), unicode.get()), "AU names leaked between instances");
  // ClassInfo loading is a user-preset restore. Its descriptive name/negative
  // number must agree with PresentPreset without changing the factory menu.
  const auto afterUser = named.presentPreset();
  const Cf<CFStringRef> afterUserName(afterUser.presetName);
  require(afterUser.presetNumber == -1 && CFEqual(afterUserName.get(), unicode.get()) &&
    CFEqual(stateName(named.save().get()), unicode.get()),
    "User ClassInfo metadata disagrees with PresentPreset");

  const auto badType = withData(initial.get(), encoded);
  CFDictionarySetValue(badType.get(), CFSTR(kAUPresetNameKey), kCFBooleanTrue);
  const auto missing = withData(initial.get(), encoded);
  CFDictionaryRemoveValue(missing.get(), CFSTR(kAUPresetNameKey));
  const auto oversizedText = std::string(1025, 'x');
  const Cf<CFStringRef> oversizedName(CFStringCreateWithCString(kCFAllocatorDefault,
    oversizedText.c_str(), kCFStringEncodingUTF8));
  require(oversizedName.get(), "Cannot create oversized AU metadata");
  const auto oversized = withData(initial.get(), encoded);
  CFDictionarySetValue(oversized.get(), CFSTR(kAUPresetNameKey), oversizedName.get());
  auto corrupt = encoded;corrupt[0] ^= 0xff;
  const auto invalidBody = withData(initial.get(), corrupt);
  // Matching a real factory name must not recall it before rejecting bad data.
  CFDictionarySetValue(invalidBody.get(), CFSTR(kAUPresetNameKey), CFSTR("TetoricaSPU"));
  const auto wrongIdentity = withData(initial.get(), encoded);
  const SInt32 wrong = 0;
  const Cf<CFNumberRef> wrongNumber(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &wrong));
  require(wrongNumber.get(), "Cannot create invalid AU identity");
  CFDictionarySetValue(wrongIdentity.get(), CFSTR(kAUPresetManufacturerKey), wrongNumber.get());
  CFDictionarySetValue(wrongIdentity.get(), CFSTR(kAUPresetNameKey), CFSTR("Must not commit this identity/name"));
  const auto assertRejected = [&](CFPropertyListRef bad) {
    rejected(named, bad, encoded);
    const auto saved = named.save();
    require(CFEqual(stateName(saved.get()), unicode.get()), "Rejected AU state changed its descriptive name");
  };
  const auto assertMetadataPreserved = [&] {
    const auto saved = named.save();
    require(body(saved.get()) == encoded && named.gain() == 37.f &&
      CFEqual(stateName(saved.get()), unicode.get()), "Invalid AU preset property changed name/Gain/source");
    const auto current = named.presentPreset();
    const Cf<CFStringRef> currentName(current.presetName);
    require(current.presetNumber == -1 && CFEqual(currentName.get(), unicode.get()),
      "Invalid AU preset property changed its current number/name");
  };
  const auto malformedProperties = [&] {
    const AUPreset validUser{-1, CFSTR("Must not replace name through invalid property")};
    const AUPreset invalidFactory{1, CFSTR("Unknown factory number")};
    const AUPreset nullName{-1, nullptr};
    const AUPreset wrongName{-1, reinterpret_cast<CFStringRef>(kCFBooleanTrue)};
    const AUPreset tooLong{-1, oversizedName.get()};
    const auto fail = [&](AudioUnitPropertyID property, AudioUnitScope scope, AudioUnitElement element,
                          const void* input, UInt32 bytes) {
      require(named.setPresetProperty(property, scope, element, input, bytes) != noErr,
        "Malformed AU preset property was accepted");
      assertMetadataPreserved();
    };
    for(const auto property : std::array<AudioUnitPropertyID, 2>{kAudioUnitProperty_PresentPreset, kAudioUnitProperty_CurrentPreset})
    {
      for(const auto input : {&invalidFactory, &nullName, &wrongName, &tooLong})
        fail(property, kAudioUnitScope_Global, 0, input, sizeof(*input));
      fail(property, kAudioUnitScope_Global, 0, nullptr, sizeof(validUser));
      fail(property, kAudioUnitScope_Global, 0, &validUser, sizeof(validUser)-1);
      const std::array<std::uint8_t, sizeof(AUPreset)+1> oversizedInput{};
      fail(property, kAudioUnitScope_Global, 0, oversizedInput.data(), oversizedInput.size());
      fail(property, kAudioUnitScope_Input, 0, &validUser, sizeof(validUser));
      fail(property, kAudioUnitScope_Global, 1, &validUser, sizeof(validUser));
      AUPreset output{31337, nullptr};
      UInt32 bytes=sizeof(output)-1;
      require(named.getPresetProperty(property, kAudioUnitScope_Global, 0, &output, bytes) != noErr &&
        output.presetNumber == 31337 && !output.presetName, "Short AU preset getter overwrote its output");
      bytes=sizeof(output);
      require(named.getPresetProperty(property, kAudioUnitScope_Global, 0, nullptr, bytes) != noErr,
        "Null AU preset getter output was accepted");
      require(named.getPresetProperty(property, kAudioUnitScope_Input, 0, &output, bytes) != noErr &&
        named.getPresetProperty(property, kAudioUnitScope_Global, 1, &output, bytes) != noErr &&
        output.presetNumber == 31337 && !output.presetName, "Invalid AU preset getter scope/element was accepted");
      assertMetadataPreserved();
    }
  };
  for(const auto bad : {badType.get(), missing.get(), oversized.get(), invalidBody.get(), wrongIdentity.get()})
    assertRejected(bad); // Pending engine/name remain intact before first audio.
  malformedProperties();
  require(named.renderMidi({tetorica::test::on(0, 60, 37)}) ==
    reference.renderMidi({tetorica::test::on(0, 60, 37)}), "Named restore changed first-render source/Gain PCM");
  for(const auto bad : {badType.get(), missing.get(), oversized.get(), invalidBody.get(), wrongIdentity.get()})
  {
    assertRejected(bad);
    require(named.renderMidi({}) == reference.renderMidi({}) && named.right() == reference.right(),
      "Rejected AU name/state interrupted active PCM");
  }

  malformedProperties();
  require(named.renderMidi({}) == reference.renderMidi({}) && named.right() == reference.right(),
    "Invalid AU preset properties interrupted active PCM");
  // The deprecated CurrentPreset alias follows the same bounded metadata path.
  const AUPreset alias{-7, unicode.get()};
  ok(named.setPresetProperty(kAudioUnitProperty_CurrentPreset, kAudioUnitScope_Global, 0, &alias, sizeof(alias)),
    "AU rejected the CurrentPreset user-name alias");
  const auto aliasPresent = named.presentPreset();
  const auto aliasCurrent = named.presentPreset(kAudioUnitProperty_CurrentPreset);
  const Cf<CFStringRef> aliasPresentName(aliasPresent.presetName), aliasCurrentName(aliasCurrent.presetName);
  require(aliasPresent.presetNumber == -7 && aliasCurrent.presetNumber == -7 &&
    CFEqual(aliasPresentName.get(), unicode.get()) && CFEqual(aliasCurrentName.get(), unicode.get()) &&
    CFEqual(stateName(named.save().get()), unicode.get()) && body(named.save().get()) == encoded,
    "AU CurrentPreset/PresentPreset/ClassInfo user metadata disagree");
  require(named.renderMidi({}) == reference.renderMidi({}) && named.right() == reference.right(),
    "AU user-property rename recalled or interrupted the active sound");

  // Public factory recall must restore its immutable default chunk and name,
  // replacing the user name without overwriting the factory menu entry.
  named.restoreFactoryPreset(0);
  const auto restoredDefault = named.save();
  require(body(restoredDefault.get()) == defaultBody && named.gain() == 100.f &&
    CFEqual(stateName(restoredDefault.get()), CFSTR("TetoricaSPU")),
    "AU PresentPreset recall retained user metadata or failed to restore builtin defaults");
  const auto recalled = named.presentPreset();
  const Cf<CFStringRef> recalledName(recalled.presetName);
  require(recalled.presetNumber == 0 && CFEqual(recalledName.get(), stateName(restoredDefault.get())) &&
    CFEqual(factory->presetName, CFSTR("TetoricaSPU")), "AU factory name changed after user-state restore");
  Instance fresh(component);
  require(named.renderMidi({tetorica::test::on(0, 60, 37)}) ==
    fresh.renderMidi({tetorica::test::on(0, 60, 37)}) && named.right() == fresh.right(),
    "AU named factory recall did not restore fresh builtin audio");
  std::cout << "PASS AU preset names factoryPresentClassInfoConsistent=true userUnicodeEmpty1024=true "
    "mutableInputCopied=true instanceNamesIndependent=true noRecallByName=true "
    "userPresentCurrentClassInfo=true invalidNameIdentityBodyAtomic=true malformedPropertyAtomic=true "
    "pendingActivePCMPreserved=true renameKeepsActivePCM=true factoryRecallDefaultPCM=true\n";
}
} // namespace

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 3)
  {
    std::cerr << "Usage: tetorica_au_state_check /path/TetoricaSPU.component [fixture.vab | --software-audio-only]\n";
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
      0x00000000, factory);
    require(component, "Cannot register local AU factory");

    const bool softwareOnly=argc==3&&std::string(argv[2])=="--software-audio-only";
    const char* fixture = argc == 3&&!softwareOnly ? argv[2] : VAB_FIXTURE_DIR "/spu-synth-demo.vab";
    std::ifstream input(fixture, std::ios::binary | std::ios::ate);
    require(input.good(), "Cannot open original VAB fixture");
    const auto length = input.tellg();
    require(length > 0 && length <= static_cast<std::streamoff>(tetorica::vab::kMaxFileBytes), "Fixture is oversized");
    std::vector<std::uint8_t> bank(static_cast<std::size_t>(length));
    input.seekg(0);
    require(bool(input.read(reinterpret_cast<char*>(bank.data()), bank.size())), "Cannot read VAB fixture");
    if(softwareOnly){
      tetorica::test::checkSoftwareAudio([&]{return std::make_unique<Instance>(component);},48000,"AU state/MIDIEvent",bank);
      return 0;
    }
    checkPresetNames(component, bank);
    for(const auto rate:{44100.,48000.,96000.})
      tetorica::test::checkBuiltinWide([&]{return std::make_unique<Instance>(component,rate);},
        rate,"AU state/MIDIEvent");
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
    tetorica::test::checkGainSmoothing([&] { return std::make_unique<Instance>(component); },48000,"AU parameters/MIDIEvent");
    tetorica::test::checkControllers([&] { return std::make_unique<Instance>(component); },48000,"AU MIDIEvent");
    tetorica::test::checkChannelStopsPmon([&] { return std::make_unique<Instance>(component); },48000,"AU MIDIEvent");
    tetorica::test::checkEnvelopes([&] { return std::make_unique<Instance>(component); },48000,"AU parameters",bank);
    tetorica::test::checkReverbs([&] { return std::make_unique<Instance>(component); },48000,"AU parameters",bank);
    tetorica::test::checkPcmCycles([&] { return std::make_unique<Instance>(component); },48000,"AU state/MIDIEvent");
    tetorica::test::checkNoise([&] { return std::make_unique<Instance>(component); },48000,"AU parameters/MIDIEvent",bank);
    tetorica::test::checkPmon([&] { return std::make_unique<Instance>(component); },48000,"AU parameters/MIDIEvent",bank);
    tetorica::test::checkMorph([&] { return std::make_unique<Instance>(component); },48000,"AU parameters/MIDIEvent",bank);
    tetorica::test::checkWebInstruments([&] { return std::make_unique<Instance>(component); },48000,"AU state/MIDIEvent",bank);
    tetorica::test::checkWavImports([&] { return std::make_unique<Instance>(component); },48000,"AU state/MIDIEvent");
    tetorica::test::checkSoftwareAudio([&] { return std::make_unique<Instance>(component); },48000,"AU state/MIDIEvent",bank);
    tetorica::test::checkLoopRepeats([&] { return std::make_unique<Instance>(component); },48000,"AU state/MIDIEvent",bank);
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
