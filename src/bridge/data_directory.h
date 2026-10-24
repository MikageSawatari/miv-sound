#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace effetune::vst {

inline constexpr char kDataDirectoryError[] =
    "mIV Sound requires MIV_SOUND_DATA_DIR to be a non-empty absolute path";

// An empty result is deliberately unusable: never fall back to the host's
// APPDATA, home, temporary directory, or current working directory.
[[nodiscard]] inline std::filesystem::path soundDataDirectory() {
#if defined(_WIN32)
  const auto length = GetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", nullptr, 0);
  if (length == 0) return {};
  std::wstring value(length, L'\0');
  const auto copied = GetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", value.data(), length);
  if (copied == 0 || copied >= length) return {};
  value.resize(copied);
#else
  const auto *environment = std::getenv("MIV_SOUND_DATA_DIR");
  if (environment == nullptr || *environment == '\0') return {};
  const std::string value(environment);
#endif
  const std::filesystem::path directory(value);
  return directory.is_absolute() ? directory : std::filesystem::path{};
}

} // namespace effetune::vst
