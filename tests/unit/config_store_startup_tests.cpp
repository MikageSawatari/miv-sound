#include "bridge/config_store.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Fixture {
  std::filesystem::path root = std::filesystem::temp_directory_path() /
      ("miv-startup-config-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  effetune::vst::ConfigStore store{root / "plugin" / "config.json"};
  std::string content, error;
  ~Fixture() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
};
constexpr auto lastView = R"({"startupView":"last"})";

void firstRun() {
  Fixture f;
  expect(f.store.loadForStartup(f.content, &f.error), "first-run config loads");
  expect(f.content == lastView && f.error.empty(), "first run selects last view");
  expect(f.store.load(f.content) && f.content == lastView, "first run persists last view without history");
  expect(!std::filesystem::exists(f.root / "webview"), "config loading creates no profile");
  expect(f.store.loadForStartup(f.content) && f.content == lastView, "later launch retains last-view preference");
}

void existingConfig(const std::string &saved, bool profile) {
  Fixture f;
  expect(f.store.save(saved), "existing config fixture saves");
  if (profile) std::filesystem::create_directories(f.root / "webview");
  const auto modified = std::filesystem::last_write_time(f.store.path());
  expect(f.store.loadForStartup(f.content, &f.error) && f.content == saved,
         "existing config remains byte-for-byte unchanged");
  expect(f.error.empty() && std::filesystem::last_write_time(f.store.path()) == modified,
         "existing config is not rewritten");
  expect(f.store.load(f.content) && f.content == saved, "existing config remains on disk");
}

void existingProfile() {
  Fixture f;
  std::filesystem::create_directories(f.root / "webview");
  expect(f.store.loadForStartup(f.content, &f.error) && f.content == "{}" && f.error.empty(),
         "migrated profile keeps effects default without startupView");
  expect(!std::filesystem::exists(f.store.path().parent_path()), "profile-only user gets no config write");
}

void writeFailure() {
  Fixture f;
  auto temporary = f.store.path();
  temporary += ".tmp";
  std::filesystem::create_directories(temporary);
  expect(f.store.loadForStartup(f.content, &f.error) && f.content == lastView,
         "failed write retains last view without history for this session");
  expect(!f.error.empty(), "failed write supplies a diagnostic to the processor");
  expect(!std::filesystem::exists(f.store.path()), "failed write leaves no config file");
}
} // namespace

int main() {
  try {
    firstRun();
    existingConfig("{\n  \"language\": \"ja\"\n}\n", false);
    existingConfig(R"({"startupView":"effects"})", false);
    existingConfig(R"({"startupView":"effects"})", true);
    existingConfig(R"({"startupView":"visualizer"})", true);
    existingConfig(R"({"startupView":"library"})", false);
    existingConfig(R"({"startupView":"last","lastStartupView":"effects"})", true);
    existingProfile();
    writeFailure();
    std::cout << "9 startup config cases passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
