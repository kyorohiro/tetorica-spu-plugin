// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Bounded host-format adapters. These deliberately avoid the pinned framework's
// unbounded stream/CFData copies and AU's pre-validation named-preset restore.
// State preparation and restore are non-RT, quiescent-host lifecycle operations.
#include "TetoricaSPU.h"
#include "InstrumentState.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

#if defined(VST3_API)
namespace {
bool readVstState(Steinberg::IBStream* stream, std::vector<std::uint8_t>& bytes)
{
  if (!stream)
    return false;
  constexpr auto maximum = tetorica::kMaxStateBytes + 4;
  std::array<std::uint8_t, 4096> buffer{};
  for (;;)
  {
    Steinberg::int32 count = 0;
    const auto status = stream->read(buffer.data(), static_cast<Steinberg::int32>(buffer.size()), &count);
    if (count < 0 || count > static_cast<Steinberg::int32>(buffer.size()) ||
        (status != Steinberg::kResultOk && status != Steinberg::kResultFalse))
      return false;
    if (!count)
      return bytes.size() >= 12;
    if (static_cast<std::size_t>(count) > maximum - bytes.size())
      return false;
    bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
  }
}

bool writeVstBytes(Steinberg::IBStream* stream, const std::uint8_t* bytes, std::size_t length)
{
  while (length)
  {
    Steinberg::int32 count = 0;
    const auto requested = static_cast<Steinberg::int32>(length);
    const auto status = stream->write(const_cast<std::uint8_t*>(bytes), requested, &count);
    if ((status != Steinberg::kResultOk && status != Steinberg::kResultFalse) ||
        count <= 0 || count > requested)
      return false;
    bytes += count;
    length -= static_cast<std::size_t>(count);
  }
  return true;
}
} // namespace

Steinberg::tresult PLUGIN_API TetoricaSPU::getState(Steinberg::IBStream* stream)
{
  if (!stream)
    return Steinberg::kResultFalse;
  try
  {
    iplug::IByteChunk chunk;
    if (!SerializeState(chunk) || chunk.Size() <= 0 ||
        static_cast<std::size_t>(chunk.Size()) > tetorica::kMaxStateBytes)
      return Steinberg::kResultFalse;
    const std::array<std::uint8_t, 4> bypass{{static_cast<std::uint8_t>(GetBypassed()), 0, 0, 0}};
    if (!writeVstBytes(stream, chunk.GetData(), static_cast<std::size_t>(chunk.Size())) ||
        !writeVstBytes(stream, bypass.data(), bypass.size()))
      return Steinberg::kResultFalse;
    return Steinberg::kResultOk;
  }
  catch (...)
  {
    return Steinberg::kResultFalse;
  }
}

Steinberg::tresult PLUGIN_API TetoricaSPU::setState(Steinberg::IBStream* stream)
{
  std::uint32_t bypass = 0;
  try
  {
    std::vector<std::uint8_t> bytes;
    if (!readVstState(stream, bytes))
      return Steinberg::kResultFalse;
    const std::size_t bodySize = bytes.size() - 4;
    bypass = static_cast<std::uint32_t>(bytes[bodySize]) |
      (static_cast<std::uint32_t>(bytes[bodySize + 1]) << 8) |
      (static_cast<std::uint32_t>(bytes[bodySize + 2]) << 16) |
      (static_cast<std::uint32_t>(bytes[bodySize + 3]) << 24);
    if (bypass > 1)
      return Steinberg::kResultFalse;
    iplug::IByteChunk chunk;
    if (chunk.PutBytes(bytes.data(), static_cast<int>(bodySize)) != static_cast<int>(bodySize))
      return Steinberg::kResultFalse;
    // UnserializeState validates the complete inner envelope and prepares the
    // replacement synchronously before committing any parameter/engine state.
    if (UnserializeState(chunk, 0) != static_cast<int>(bodySize))
      return Steinberg::kResultFalse;
  }
  catch (...)
  {
    return Steinberg::kResultFalse;
  }

  SetBypassed(bypass != 0);
  // State is committed. A UI notification failure must neither escape the host
  // ABI nor report a transactional restore failure after changing the sound.
  try
  {
    UpdateParams(this, static_cast<int>(bypass));
    OnRestoreState();
  }
  catch (...) {}
  return Steinberg::kResultOk;
}
#endif

#if defined(AU_API)
static_assert(std::is_same_v<decltype(&TetoricaSPU::AuGetProperty), AudioUnitGetPropertyProc>);
static_assert(std::is_same_v<decltype(&TetoricaSPU::AuSetProperty), AudioUnitSetPropertyProc>);
// Recent Apple SDKs annotate the official parameter pointers nonblocking;
// older SDKs do not. Check their callable ABI without requiring that attribute
// on the pinned framework's forwarding methods.
static_assert(std::is_invocable_r_v<OSStatus, AudioUnitSetParameterProc, void*,
  AudioUnitParameterID, AudioUnitScope, AudioUnitElement, AudioUnitParameterValue, UInt32>);
static_assert(std::is_invocable_r_v<OSStatus, AudioUnitScheduleParametersProc, void*,
  const AudioUnitParameterEvent*, UInt32>);
namespace {
bool auPresetProperty(AudioUnitPropertyID property)
{
  return property == kAudioUnitProperty_PresentPreset || property == kAudioUnitProperty_CurrentPreset;
}

bool auNumber(CFDictionaryRef dictionary, CFStringRef key, std::int32_t& value)
{
  const auto entry = CFDictionaryGetValue(dictionary, key);
  return entry && CFGetTypeID(entry) == CFNumberGetTypeID() &&
    CFNumberGetValue(static_cast<CFNumberRef>(entry), kCFNumberSInt32Type, &value);
}

bool putAuNumber(CFMutableDictionaryRef dictionary, CFStringRef key, std::int32_t value)
{
  const auto number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
  if (!number)
    return false;
  CFDictionarySetValue(dictionary, key, number);
  CFRelease(number);
  return true;
}
} // namespace

template<> AudioComponentMethod iplug::IPlugAUFactory<TetoricaSPU, true>::Lookup(SInt16 selector)
{
  if (selector == kAudioUnitGetPropertySelect)
    return reinterpret_cast<AudioComponentMethod>(&TetoricaSPU::AuGetProperty);
  if (selector == kAudioUnitSetPropertySelect)
    return reinterpret_cast<AudioComponentMethod>(&TetoricaSPU::AuSetProperty);
  if (selector == kAudioUnitSetParameterSelect)
    return reinterpret_cast<AudioComponentMethod>(&TetoricaSPU::AuSetParameter);
  if (selector == kAudioUnitScheduleParametersSelect)
    return reinterpret_cast<AudioComponentMethod>(&TetoricaSPU::AuScheduleParameters);
  return IPlugAUFactory<IPlugAU, true>::Lookup(selector);
}

OSStatus TetoricaSPU::AuSetParameter(void* instance, AudioUnitParameterID parameter,
  AudioUnitScope scope, AudioUnitElement element, AudioUnitParameterValue value, UInt32 offset)
{
  const auto forward = reinterpret_cast<AudioUnitSetParameterProc>(
    iplug::IPlugAUFactory<iplug::IPlugAU, true>::Lookup(kAudioUnitSetParameterSelect));
  const int slot = parameter < kNumParams ? continuousSlot(parameter) : -1;
  if (slot < 0) return forward(instance, parameter, scope, element, value, offset);
  if (!instance) return kAudioUnitErr_InvalidParameter;
  if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
  if (element != 0) return kAudioUnitErr_InvalidElement;
  auto* plug = reinterpret_cast<TetoricaSPU*>(
    &static_cast<iplug::AudioComponentPlugInInstance*>(instance)->mInstanceStorage);
  const auto* param = plug->GetParam(parameter);
  if (!std::isfinite(value) || value < param->GetMin() || value > param->GetMax() ||
      offset > static_cast<UInt32>(std::numeric_limits<int>::max()))
    return kAudioUnitErr_InvalidParameter;
  // Publish the event before its public target: render must never observe a
  // future target without the corresponding offset. The continuous path
  // avoids the pinned framework's single-producer editor notification FIFO.
  plug->scheduleAuContinuous(parameter, static_cast<int>(offset), value);
  return noErr;
}

OSStatus TetoricaSPU::AuScheduleParameters(void* instance,
  const AudioUnitParameterEvent* events, UInt32 count)
{
  if (!instance || (count && !events) || count > tetorica::ContinuousAutomation::kCapacity)
    return kAudioUnitErr_InvalidParameter;
  auto* plug = reinterpret_cast<TetoricaSPU*>(
    &static_cast<iplug::AudioComponentPlugInInstance*>(instance)->mInstanceStorage);
  // Validate the complete bounded batch before publishing any of its points.
  for (UInt32 i=0; i<count; ++i)
  {
    const auto& event = events[i];
    if (event.parameter >= kNumParams) return kAudioUnitErr_InvalidParameter;
    if (event.scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
    if (event.element != 0) return kAudioUnitErr_InvalidElement;
    const auto* param = plug->GetParam(event.parameter);
    const auto valid = [param](double v) {
      return std::isfinite(v) && v >= param->GetMin() && v <= param->GetMax();
    };
    if (event.eventType == kParameterEvent_Immediate)
    {
      if (!valid(event.eventValues.immediate.value) || event.eventValues.immediate.bufferOffset >
          static_cast<UInt32>(std::numeric_limits<int>::max())) return kAudioUnitErr_InvalidParameter;
    }
    else if (event.eventType == kParameterEvent_Ramped)
    {
      if (continuousSlot(event.parameter)<0 || !event.eventValues.ramp.durationInFrames ||
          !valid(event.eventValues.ramp.startValue) || !valid(event.eventValues.ramp.endValue))
        return kAudioUnitErr_InvalidParameter;
    }
    else return kAudioUnitErr_InvalidParameter;
  }
  for (UInt32 i=0; i<count; ++i)
  {
    const auto& event = events[i];
    if (event.eventType == kParameterEvent_Immediate)
    {
      const auto& step = event.eventValues.immediate;
      const auto status = AuSetParameter(instance, event.parameter,event.scope,event.element,step.value,step.bufferOffset);
      if (status != noErr) return status;
    }
    else
    {
      const auto& ramp = event.eventValues.ramp;
      plug->scheduleAuContinuous(event.parameter, ramp.startBufferOffset, ramp.endValue,
        ramp.durationInFrames, ramp.startValue);
    }
  }
  return noErr;
}

OSStatus TetoricaSPU::AuGetProperty(void* instance, AudioUnitPropertyID property,
  AudioUnitScope scope, AudioUnitElement element, void* data, UInt32* size)
{
  if (!auPresetProperty(property))
  {
    using GetProperty = OSStatus (*)(void*, AudioUnitPropertyID, AudioUnitScope, AudioUnitElement, void*, UInt32*);
    const auto forward = reinterpret_cast<GetProperty>(
      iplug::IPlugAUFactory<iplug::IPlugAU, true>::Lookup(kAudioUnitGetPropertySelect));
    const auto status = forward(instance, property, scope, element, data, size);
    if (status == noErr && property == kAudioUnitProperty_ParameterInfo &&
        scope == kAudioUnitScope_Global && element < kNumParams &&
        continuousSlot(element) >= 0 && data && size && *size >= sizeof(AudioUnitParameterInfo))
      static_cast<AudioUnitParameterInfo*>(data)->flags |= kAudioUnitParameterFlag_CanRamp;
    return status;
  }
  if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
  if (element != 0) return kAudioUnitErr_InvalidElement;
  if (!instance || !data || !size || *size < sizeof(AUPreset))
    return kAudioUnitErr_InvalidPropertyValue;
  try
  {
    // This is the public factory storage layout used by the pinned GetPlug.
    auto* plug = reinterpret_cast<TetoricaSPU*>(
      &static_cast<iplug::AudioComponentPlugInInstance*>(instance)->mInstanceStorage);
    AUPreset preset{0, nullptr};
    {
      std::lock_guard<std::mutex> lock(plug->mStateMutex);
      preset.presetNumber = plug->mAuPresetNumber;
      preset.presetName = plug->mAuStateName ? CFStringCreateCopy(kCFAllocatorDefault, plug->mAuStateName.get()) :
        CFStringCreateWithCString(kCFAllocatorDefault, plug->GetPresetName(plug->GetCurrentPresetIdx()), kCFStringEncodingUTF8);
    }
    if (!preset.presetName) return kAudioUnitErr_InvalidPropertyValue;
    std::memcpy(data, &preset, sizeof(preset));
    *size = sizeof(preset);
    return noErr; // As in the pinned API, the host owns the returned +1 name.
  }
  catch (...) { return kAudioUnitErr_InvalidPropertyValue; }
}

OSStatus TetoricaSPU::AuSetProperty(void* instance, AudioUnitPropertyID property,
  AudioUnitScope scope, AudioUnitElement element, const void* data, UInt32 size)
{
  if (!auPresetProperty(property))
  {
    // The Apple ABI passes size by value. The pinned entry's declaration uses
    // a pointer but only forwards its bits, so call it with the official type.
    const auto forward = reinterpret_cast<AudioUnitSetPropertyProc>(
      iplug::IPlugAUFactory<iplug::IPlugAU, true>::Lookup(kAudioUnitSetPropertySelect));
    return forward(instance, property, scope, element, data, size);
  }
  if (scope != kAudioUnitScope_Global) return kAudioUnitErr_InvalidScope;
  if (element != 0) return kAudioUnitErr_InvalidElement;
  if (!instance || !data || size != sizeof(AUPreset))
    return kAudioUnitErr_InvalidPropertyValue;
  auto* plug = reinterpret_cast<TetoricaSPU*>(
    &static_cast<iplug::AudioComponentPlugInInstance*>(instance)->mInstanceStorage);
  try
  {
    AUPreset preset;
    std::memcpy(&preset, data, sizeof(preset));
    if (!preset.presetName || CFGetTypeID(preset.presetName) != CFStringGetTypeID() ||
        CFStringGetLength(preset.presetName) > 1024)
      return kAudioUnitErr_InvalidPropertyValue;
    if (preset.presetNumber >= 0)
    {
      if (preset.presetNumber >= plug->NPresets() || !plug->RestorePreset(preset.presetNumber))
        return kAudioUnitErr_InvalidPropertyValue;
      // RestorePreset calls our OnRestoreState after its validated default
      // chunk commits, resetting user metadata to the factory number/name.
    }
    else
    {
      AuStateName nextName{CFStringCreateCopy(kCFAllocatorDefault, preset.presetName), &CFRelease};
      if (!nextName) return kAudioUnitErr_InvalidPropertyValue;
      std::lock_guard<std::mutex> lock(plug->mStateMutex);
      plug->mAuStateName.swap(nextName);
      plug->mAuPresetNumber = preset.presetNumber;
    }
  }
  catch (...) { return kAudioUnitErr_InvalidPropertyValue; }
  // Metadata-only user renames do not recall a sound or reset live MIDI. Host
  // callbacks are after the complete commit and outside every state lock.
  try { plug->InformHostOfPresetChange(); } catch (...) {}
  return noErr;
}

OSStatus TetoricaSPU::GetState(CFPropertyListRef* output)
{
  if (!output)
    return kAudioUnitErr_InvalidPropertyValue;
  *output = nullptr;
  CFMutableDictionaryRef dictionary = nullptr;
  CFDataRef data = nullptr;
  AuStateName presetName{nullptr, &CFRelease};
  try
  {
    iplug::IByteChunk chunk;
    if (!SerializeState(chunk) || chunk.Size() <= 0 ||
        static_cast<std::size_t>(chunk.Size()) > tetorica::kMaxStateBytes)
      return kAudioUnitErr_InvalidPropertyValue;
    {
      std::lock_guard<std::mutex> lock(mStateMutex);
      presetName.reset(mAuStateName ? CFStringCreateCopy(kCFAllocatorDefault, mAuStateName.get()) :
        CFStringCreateWithCString(kCFAllocatorDefault, GetPresetName(GetCurrentPresetIdx()), kCFStringEncodingUTF8));
    }
    if (!presetName)
      return kAudioUnitErr_InvalidPropertyValue;
    dictionary = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!dictionary)
      return kAudioUnitErr_InvalidPropertyValue;
    if (!putAuNumber(dictionary, CFSTR(kAUPresetVersionKey), GetPluginVersion(false)) ||
        !putAuNumber(dictionary, CFSTR(kAUPresetTypeKey), GetAUPluginType()) ||
        !putAuNumber(dictionary, CFSTR(kAUPresetSubtypeKey), GetUniqueID()) ||
        !putAuNumber(dictionary, CFSTR(kAUPresetManufacturerKey), GetMfrID()))
    {
      CFRelease(dictionary);
      return kAudioUnitErr_InvalidPropertyValue;
    }
    data = CFDataCreate(kCFAllocatorDefault, chunk.GetData(), chunk.Size());
    if (!data)
    {
      CFRelease(dictionary);
      return kAudioUnitErr_InvalidPropertyValue;
    }
    CFDictionarySetValue(dictionary, CFSTR(kAUPresetNameKey), presetName.get());
    CFDictionarySetValue(dictionary, CFSTR(kAUPresetDataKey), data);
    CFRelease(data);
    data = nullptr;
    *output = dictionary; // Host owns the returned +1 reference.
    return noErr;
  }
  catch (...)
  {
    if (data)
      CFRelease(data);
    if (dictionary)
      CFRelease(dictionary);
    return kAudioUnitErr_InvalidPropertyValue;
  }
}

OSStatus TetoricaSPU::SetState(CFPropertyListRef propertyList)
{
  if (!propertyList || CFGetTypeID(propertyList) != CFDictionaryGetTypeID())
    return kAudioUnitErr_InvalidPropertyValue;
  AuStateName nextName{nullptr, &CFRelease};
  try
  {
    const auto dictionary = static_cast<CFDictionaryRef>(propertyList);
    std::int32_t version = 0, type = 0, subtype = 0, manufacturer = 0;
    if (!auNumber(dictionary, CFSTR(kAUPresetVersionKey), version) || version < 0 ||
        !auNumber(dictionary, CFSTR(kAUPresetTypeKey), type) || type != GetAUPluginType() ||
        !auNumber(dictionary, CFSTR(kAUPresetSubtypeKey), subtype) || subtype != GetUniqueID() ||
        !auNumber(dictionary, CFSTR(kAUPresetManufacturerKey), manufacturer) || manufacturer != GetMfrID())
      return kAudioUnitErr_InvalidPropertyValue;
    const auto name = CFDictionaryGetValue(dictionary, CFSTR(kAUPresetNameKey));
    const auto data = CFDictionaryGetValue(dictionary, CFSTR(kAUPresetDataKey));
    if (!name || CFGetTypeID(name) != CFStringGetTypeID() ||
        CFStringGetLength(static_cast<CFStringRef>(name)) > 1024 ||
        !data || CFGetTypeID(data) != CFDataGetTypeID())
      return kAudioUnitErr_InvalidPropertyValue;
    const auto dataRef = static_cast<CFDataRef>(data);
    const CFIndex size = CFDataGetLength(dataRef);
    if (size <= 0 || static_cast<std::size_t>(size) > tetorica::kMaxStateBytes)
      return kAudioUnitErr_InvalidPropertyValue;
    // Copy mutable host strings, and complete all potentially failing name
    // allocation before the transactional engine/parameter restore.
    nextName.reset(CFStringCreateCopy(kCFAllocatorDefault, static_cast<CFStringRef>(name)));
    if (!nextName)
      return kAudioUnitErr_InvalidPropertyValue;
    iplug::IByteChunk chunk;
    if (chunk.PutBytes(CFDataGetBytePtr(dataRef), static_cast<int>(size)) != size ||
        UnserializeState(chunk, 0) != size)
      return kAudioUnitErr_InvalidPropertyValue;
    // Do not RestorePreset(name): that mutates parameters before validation in
    // the upstream adapter. Name is descriptive metadata, not an instruction.
    {
      std::lock_guard<std::mutex> lock(mStateMutex);
      mAuStateName.swap(nextName); // No allocation after the sound commits.
      mAuPresetNumber = -1;
    }
  }
  catch (...)
  {
    return kAudioUnitErr_InvalidPropertyValue;
  }
  // Qualified notification preserves the freshly committed ClassInfo name.
  // Factory preset restores use our override below to return to its own name.
  try { iplug::Plugin::OnRestoreState(); } catch (...) {}
  return noErr;
}

void TetoricaSPU::OnRestoreState()
{
  {
    std::lock_guard<std::mutex> lock(mStateMutex);
    mAuStateName.reset();
    mAuPresetNumber = GetCurrentPresetIdx();
  }
  // Host/editor callbacks can synchronously re-enter ClassInfo serialization.
  // Do not retain the state mutex while forwarding the framework notification.
  try { iplug::Plugin::OnRestoreState(); } catch (...) {}
}
#endif
