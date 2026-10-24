#pragma once

#include "crt_dialog_suppression.h"

#include <filesystem>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace effetune::vst::testing {

inline std::optional<std::wstring> testEnvironmentValue(const wchar_t *name) {
  SetLastError(ERROR_SUCCESS);
  const auto size = GetEnvironmentVariableW(name, nullptr, 0);
  if (size == 0) {
    if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) return std::nullopt;
    if (GetLastError() == ERROR_SUCCESS) return std::wstring{};
    throw std::runtime_error("Cannot snapshot test environment variable");
  }
  std::wstring value(size, L'\0');
  SetLastError(ERROR_SUCCESS);
  const auto copied = GetEnvironmentVariableW(name, value.data(), size);
  if (copied == 0 && GetLastError() == ERROR_SUCCESS) return std::wstring{};
  if (copied == 0 || copied >= size)
    throw std::runtime_error("Test environment changed while being captured");
  value.resize(copied);
  return value;
}

// Snapshot before ANY mutations; restore even if an assertion throws. Restoration
// failure is retained by the owning test host, rather than lost in a destructor.
class ScopedTestEnvironment {
public:
  ScopedTestEnvironment(std::initializer_list<const wchar_t *> names, bool &cleanupFailed)
      : directory_(std::filesystem::current_path()), cleanupFailed_(cleanupFailed) {
    for (const auto *name : names) values_.emplace_back(name, testEnvironmentValue(name));
  }
  ~ScopedTestEnvironment() {
    bool restored = SetCurrentDirectoryW(directory_.c_str()) != FALSE;
    for (const auto &[name, value] : values_)
      if (!SetEnvironmentVariableW(name.c_str(), value ? value->c_str() : nullptr)) restored = false;
    if (!restored) {
      std::fputs("Test environment/current-directory restoration failed\n", stderr);
      cleanupFailed_ = true;
    }
  }
  ScopedTestEnvironment(const ScopedTestEnvironment &) = delete;
  ScopedTestEnvironment &operator=(const ScopedTestEnvironment &) = delete;

  void set(const wchar_t *name, const wchar_t *value) {
    for (const auto &[savedName, savedValue] : values_) {
      (void)savedValue;
      if (savedName == name) {
        if (!SetEnvironmentVariableW(name, value))
          throw std::runtime_error("Cannot set test environment variable");
        return;
      }
    }
    throw std::runtime_error("Attempted to modify an unsnapshotted test environment variable");
  }

private:
  std::filesystem::path directory_;
  std::vector<std::pair<std::wstring, std::optional<std::wstring>>> values_;
  bool &cleanupFailed_;
};

inline std::filesystem::path recreateTestSandbox(const std::filesystem::path &root,
                                               const wchar_t *leaf) {
  namespace fs = std::filesystem;
  const fs::path name(leaf);
  if (!root.is_absolute() || name.empty() || name.has_parent_path() || name == L"." || name == L"..")
    throw std::runtime_error("Unsafe test sandbox path");
  const auto base = fs::weakly_canonical(root);
  const auto target = base / name;
  // Verify the resolved absolute target is precisely the requested child. Never
  // recursively delete the root, another child, or a junction/symlink target.
  if (target == base || target.parent_path() != base || fs::weakly_canonical(target) != target)
    throw std::runtime_error("Test sandbox resolves outside its intended directory");
  fs::remove_all(target);
  fs::create_directories(target);
  return target;
}

} // namespace effetune::vst::testing
