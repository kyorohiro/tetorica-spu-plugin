// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 kyorohiro
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace tetorica::psf {

inline constexpr std::size_t kMaxFileBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxFiles = 512;
inline constexpr std::size_t kMaxSetBytes = 64 * 1024 * 1024;

struct File { std::string path; std::vector<std::uint8_t> bytes; };
struct Bank { std::vector<std::uint8_t> bytes; std::uint32_t address = 0; };
struct Diagnostic { std::uint32_t address = 0; std::string code, reason; };
struct Result {
  std::vector<Bank> banks;
  std::vector<Diagnostic> diagnostics;
  std::vector<std::string> loadedFiles;
  std::size_t candidates = 0, expandedBytes = 0;
  std::string entryPath;
};

// Selected files only: no filesystem/network access or PS1 CPU execution.
// Paths are selected-set-relative. Header, CRC, size and path failures throw
// std::runtime_error. Libraries alone may be selected as an entry.
std::vector<std::string> entries(const std::vector<File>& files);
Result extract(const std::vector<File>& files, const std::string& entryPath,
               const std::function<bool()>& cancelled = {});

} // namespace tetorica::psf
