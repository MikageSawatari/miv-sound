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
inline constexpr char kWebViewDataDirectoryOverrideError[] =
    "mIV Sound cannot guarantee its data directory while WEBVIEW2_USER_DATA_FOLDER "
    "is non-empty; unset it before loading mIV Sound";

struct SoundDataDirectory {
  std::filesystem::path path;
  const char *error = nullptr;
};

// An empty result is deliberately unusable: never fall back to the host's
// APPDATA, home, temporary directory, or current working directory.
[[nodiscard]] inline SoundDataDirectory resolveSoundDataDirectory() {
#if defined(_WIN32)
  // WebView2 gives this process-wide override priority over its API argument.
  // Refuse to start rather than silently write outside mIV's data directory.
  if (GetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", nullptr, 0) > 1)
    return {{}, kWebViewDataDirectoryOverrideError};
  const auto length = GetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", nullptr, 0);
  if (length == 0) return {{}, kDataDirectoryError};
  std::wstring value(length, L'\0');
  const auto copied = GetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", value.data(), length);
  if (copied == 0 || copied >= length) return {{}, kDataDirectoryError};
  value.resize(copied);
#else
  const auto *environment = std::getenv("MIV_SOUND_DATA_DIR");
  if (environment == nullptr || *environment == '\0') return {{}, kDataDirectoryError};
  const std::string value(environment);
#endif
  const std::filesystem::path directory(value);
  return directory.is_absolute() ? SoundDataDirectory{directory, nullptr}
                                 : SoundDataDirectory{{}, kDataDirectoryError};
}

[[nodiscard]] inline std::filesystem::path soundDataDirectory() {
  return resolveSoundDataDirectory().path;
}

} // namespace effetune::vst
