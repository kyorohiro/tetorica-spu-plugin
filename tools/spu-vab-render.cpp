// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#include "SpuInstrument.h"
#include "VabBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> readBank(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) throw std::runtime_error("Cannot open input bank");
  const auto length = file.tellg();
  if (length < 0 || length > static_cast<std::streamoff>(tetorica::vab::kMaxFileBytes))
    throw std::runtime_error("Input bank exceeds 1 MiB");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    throw std::runtime_error("Cannot read complete input bank");
  return bytes;
}

void word(std::FILE* file, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i)
    if (std::fputc(static_cast<int>((value >> (8 * i)) & 255), file) == EOF)
      throw std::runtime_error("Cannot write output WAV");
}

void writeWave(const char* path, const std::vector<float>& left, const std::vector<float>& right) {
  // Exclusive create: an audition must not replace an existing recording.
  std::FILE* file = std::fopen(path, "wbx");
  if (!file) throw std::runtime_error("Cannot create WAV (choose a new output path)");
  try {
    const auto size = static_cast<std::uint32_t>(left.size() * 4);
    if (std::fwrite("RIFF", 1, 4, file) != 4) throw std::runtime_error("Cannot write WAV header");
    word(file, 36 + size, 4);
    if (std::fwrite("WAVEfmt ", 1, 8, file) != 8) throw std::runtime_error("Cannot write WAV header");
    word(file, 16, 4); word(file, 1, 2); word(file, 2, 2); word(file, 48000, 4);
    word(file, 48000 * 4, 4); word(file, 4, 2); word(file, 16, 2);
    if (std::fwrite("data", 1, 4, file) != 4) throw std::runtime_error("Cannot write WAV header");
    word(file, size, 4);
    for (std::size_t i = 0; i < left.size(); ++i) {
      for (float sample : {left[i], right[i]}) {
        if (!std::isfinite(sample)) throw std::runtime_error("Non-finite rendered sample");
        word(file, static_cast<std::uint16_t>(std::lround(std::clamp(sample, -1.f, 1.f) * 32767)), 2);
      }
    }
  } catch (...) {
    std::fclose(file);
    throw;
  }
  if (std::fclose(file) != 0) throw std::runtime_error("Cannot finish WAV output");
}
}

int main(int argc, char** argv) {
  if (argc != 2 && argc != 4) {
    std::fprintf(stderr, "Usage: spu_vab_render input.vab [flat-tone-index new-output.wav]\n"
                         "Lists complete VAB tones; optionally renders MIDI 60 for one second plus one second release.\n");
    return 2;
  }
  try {
    const auto bytes = readBank(argv[1]);
    const auto bank = tetorica::vab::parse({bytes.data(), bytes.size()});
    std::printf("VAB v%u: %zu Programs, %zu Tones, %zu Samples\n", bank.version,
                bank.programs.size(), bank.tones.size(), bank.samples.size());
    for (std::size_t i = 0; i < bank.tones.size(); ++i) {
      const auto& tone = bank.tones[i];
      std::printf("[%zu] Program %u Tone %u Sample %u, keys %u..%u\n", i,
                  unsigned(tone.program), unsigned(tone.index), unsigned(tone.sample),
                  unsigned(tone.keyLow), unsigned(tone.keyHigh));
    }
    if (argc == 2) return 0;
    const std::string choice(argv[2]);
    if (choice.empty() || choice.find_first_not_of("0123456789") != std::string::npos)
      throw std::runtime_error("Tone index must be a nonnegative integer");
    std::size_t consumed = 0;
    const auto selected = std::stoull(choice, &consumed);
    if (consumed != choice.size() || selected >= bank.tones.size())
      throw std::runtime_error("Tone index is outside this bank");
    const auto tone = tetorica::vab::prepareTone(bank, static_cast<std::size_t>(selected));
    tetorica::SpuInstrument synth(tone);
    if (!synth.prepare(48000)) throw std::runtime_error("Cannot prepare Native SPU");
    std::vector<float> left(96000), right(96000);
    synth.noteOn(60, 127);
    synth.render(left.data(), right.data(), 48000);
    synth.noteOff(60);
    synth.render(left.data() + 48000, right.data() + 48000, 48000);
    if (!synth.healthy()) throw std::runtime_error("Native SPU rendering failed");
    writeWave(argv[3], left, right);
    std::printf("Wrote %s (48 kHz stereo PCM16, 2 seconds; shared Reverb off).\n", argv[3]);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "Error: %s\n", error.what());
    return 1;
  }
  return 0;
}
