// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
// Bounded host-format adapters. These deliberately avoid the pinned framework's
// unbounded stream/CFData copies and AU's pre-validation named-preset restore.
// State preparation and restore are non-RT, quiescent-host lifecycle operations.
#include "TetoricaSPU.h"
#include "InstrumentState.h"

#include <array>
#include <cstdint>
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
namespace {
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

OSStatus TetoricaSPU::GetState(CFPropertyListRef* output)
{
  if (!output)
    return kAudioUnitErr_InvalidPropertyValue;
  *output = nullptr;
  CFMutableDictionaryRef dictionary = nullptr;
  CFDataRef data = nullptr;
  try
  {
    iplug::IByteChunk chunk;
    if (!SerializeState(chunk) || chunk.Size() <= 0 ||
        static_cast<std::size_t>(chunk.Size()) > tetorica::kMaxStateBytes)
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
    CFDictionarySetValue(dictionary, CFSTR(kAUPresetNameKey), CFSTR("TetoricaSPU"));
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
    iplug::IByteChunk chunk;
    if (chunk.PutBytes(CFDataGetBytePtr(dataRef), static_cast<int>(size)) != size ||
        UnserializeState(chunk, 0) != size)
      return kAudioUnitErr_InvalidPropertyValue;
    // Do not RestorePreset(name): that mutates parameters before validation in
    // the upstream adapter. Name is descriptive metadata, not an instruction.
  }
  catch (...)
  {
    return kAudioUnitErr_InvalidPropertyValue;
  }
  try { OnRestoreState(); } catch (...) {}
  return noErr;
}
#endif
