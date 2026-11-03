#include "plugin/plugin_processor.h"
#include "plugin/plugin_view.h"
#include "bridge/data_directory.h"
#include "bridge/webview_host.h"
#include "choc/text/choc_JSON.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "../support/scoped_test_environment.h"

namespace {
std::size_t webViewConstructions = 0;
std::size_t webViewAttachments = 0;
std::string configAtWebViewConstruction;
bool failWebViewConstruction = false;
bool cleanupFailed = false;
}

// This executable deliberately links these doubles instead of webview_host.cpp.
// It exercises the real processor/view entry points without loading WebView2.
namespace effetune::vst {
struct WebViewHostState { void *owner = nullptr; };
WebViewHost::WebViewHost(MessageHandler handler, std::filesystem::path, bool) {
  ++webViewConstructions;
  configAtWebViewConstruction = handler(R"({"type":"config/load"})");
  if (failWebViewConstruction) throw std::runtime_error("WebView runtime unavailable in test");
  state_ = std::make_shared<WebViewHostState>();
}
WebViewHost::~WebViewHost() = default;
void WebViewHost::shutdown() noexcept {}
bool WebViewHost::attach(void *owner, void *, std::int32_t, std::int32_t) {
  ++webViewAttachments;
  state_->owner = owner;
  return true;
}
bool WebViewHost::isAttachedTo(void *owner) const noexcept { return state_->owner == owner; }
void WebViewHost::resize(void *, std::int32_t, std::int32_t) noexcept {}
void WebViewHost::detach(void *) noexcept { state_->owner = nullptr; }
bool WebViewHost::loaded() const noexcept { return false; }
bool WebViewHost::evaluate(std::string, EvaluationHandler) { return false; }
WebViewStatus WebViewHost::status() const noexcept { return WebViewStatus::runtimeUnavailable; }
void WebViewHost::serviceInitialisation() noexcept {}
std::string WebViewHost::diagnosticText(WebViewStatus) { return "Test WebView double"; }
} // namespace effetune::vst

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace effetune::vst;
using plugin::EffeTuneProcessor;
namespace fs = std::filesystem;

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input), "read test file");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

choc::value::Value config(EffeTuneProcessor &processor) {
  const auto response = choc::json::parse(processor.handleUiMessage(R"({"type":"config/load"})"));
  expect(response["ok"].getBool(), "config/load succeeds");
  return choc::value::Value(response["config"]);
}

struct Fixture {
  testing::ScopedTestEnvironment environment{
      {L"MIV_SOUND_DATA_DIR", L"WEBVIEW2_USER_DATA_FOLDER", L"APPDATA", L"HOME", L"USERPROFILE"}, cleanupFailed};
  fs::path sandbox, data;
  Fixture(const fs::path &root, const wchar_t *name)
      : sandbox(testing::recreateTestSandbox(root, name)), data(sandbox / "sound-data") {
    environment.set(L"MIV_SOUND_DATA_DIR", data.c_str());
    environment.set(L"WEBVIEW2_USER_DATA_FOLDER", nullptr);
    environment.set(L"APPDATA", (sandbox / "appdata").c_str());
    environment.set(L"HOME", (sandbox / "home").c_str());
    environment.set(L"USERPROFILE", (sandbox / "home").c_str());
    fs::current_path(sandbox);
    configAtWebViewConstruction.clear();
  }
  fs::path configPath() const { return data / "plugin" / "config.json"; }
};

void expectEditorRefused(EffeTuneProcessor &processor) {
  const auto constructions = webViewConstructions, attachments = webViewAttachments;
  expect(processor.createView(ViewType::kEditor) == nullptr, "createView refuses an uninitialized processor");
  expect(!processor.attachEditor(&processor, &processor, 640, 480), "direct attachment refuses an uninitialized processor");
  // Even a view obtained by bypassing createView cannot create a WebView.
  auto bypass = owned(plugin::EffeTuneView::create(&processor));
  expect(bypass->attached(&processor, kPlatformTypeHWND) == kResultFalse,
         "attached explicitly fails before successful initialize");
  expect(webViewConstructions == constructions && webViewAttachments == attachments,
         "refused editor entry points never reach WebView construction or attachment");
}

void firstRun(const fs::path &root, bool missingRuntime) {
  Fixture f(root, missingRuntime ? L"missing-runtime" : L"first-run");
  auto processor = owned(new EffeTuneProcessor);
  expect(fs::is_empty(f.sandbox), "constructing a new processor writes nothing");
  expectEditorRefused(*processor);
  expect(fs::is_empty(f.sandbox), "refused early editor access writes nothing");
  expect(!config(*processor).hasObjectMember("startupView"), "constructor retains read-only missing-config behavior");
  failWebViewConstruction = missingRuntime;
  const auto result = processor->initialize(nullptr);
  failWebViewConstruction = false;
  expect(result == kResultOk, "new directory initializes even without a WebView runtime");
  expect(choc::json::parse(read(f.configPath()))["startupView"].getString() == "last",
         "successful initialize persists the first-run startup view");
  expect(config(*processor)["startupView"].getString() == "last", "config/load reflects first-run default");
  expect(choc::json::parse(configAtWebViewConstruction)["config"]["startupView"].getString() == "last",
         "config is published before any WebView can load it");
  expect(!fs::exists(f.data / "webview"), "processor tests never create a WebView2 profile");
  auto view = owned(processor->createView(ViewType::kEditor));
  expect(view != nullptr, "createView succeeds after initialize");
  expect(view->attached(processor.get(), kPlatformTypeHWND) == kResultOk, "attached succeeds after initialize with the double");
  expect(view->removed() == kResultOk, "remove test editor");
  expect(processor->terminate() == kResultOk, "terminate initialized component");
  expectEditorRefused(*processor);
}

void rejected(const fs::path &root, const wchar_t *name, const wchar_t *override, bool late) {
  Fixture f(root, name);
  if (!late) f.environment.set(L"WEBVIEW2_USER_DATA_FOLDER", override);
  auto processor = owned(new EffeTuneProcessor);
  if (late) f.environment.set(L"WEBVIEW2_USER_DATA_FOLDER", override);
  const auto constructions = webViewConstructions;
  expect(processor->initialize(nullptr) == kInvalidArgument, "storage override rejects initialize");
  expectEditorRefused(*processor);
  expect(webViewConstructions == constructions, "rejected initialize never constructs a WebView");
  expect(fs::is_empty(f.sandbox),
         "rejected startup creates no config, WebView, override, APPDATA or cwd files");
}

void invalidThenRepaired(const fs::path &root) {
  Fixture f(root, L"invalid-then-repaired");
  f.environment.set(L"MIV_SOUND_DATA_DIR", L"relative");
  auto processor = owned(new EffeTuneProcessor);
  f.environment.set(L"MIV_SOUND_DATA_DIR", f.data.c_str());
  expect(processor->initialize(nullptr) == kInvalidArgument, "original invalid storage cannot be repaired after construction");
  expectEditorRefused(*processor);
  expect(fs::is_empty(f.sandbox), "original invalid storage writes nothing");
}

void existingConfig(const fs::path &root, const wchar_t *name, const std::string &saved, bool late) {
  Fixture f(root, name);
  ConfigStore store(f.configPath());
  if (!late) expect(store.save(saved), "save existing config fixture");
  auto processor = owned(new EffeTuneProcessor);
  if (late) expect(store.save(saved), "config arrives between construction and initialize");
  const auto modified = fs::last_write_time(f.configPath());
  expect(processor->initialize(nullptr) == kResultOk, "initialize existing config");
  expect(read(f.configPath()) == saved && fs::last_write_time(f.configPath()) == modified,
         "existing config is not rewritten");
  const auto expected = choc::json::parse(saved);
  expect(config(*processor)["startupView"].getWithDefault<std::string>("") ==
             expected["startupView"].getWithDefault<std::string>(""), "existing startup view remains unchanged");
  expect(config(*processor)["columns"].getInt64() == 3, "config/load retains existing columns");
  expect(processor->terminate() == kResultOk, "terminate existing-config component");
}

void existingProfile(const fs::path &root, bool late) {
  Fixture f(root, late ? L"late-profile" : L"existing-profile");
  const auto marker = f.data / "webview" / "migrated-profile";
  auto createProfile = [&] {
    fs::create_directories(marker.parent_path());
    std::ofstream output(marker, std::ios::binary);
    output << "saved layout and preset data";
  };
  if (!late) createProfile();
  auto processor = owned(new EffeTuneProcessor);
  if (late) createProfile();
  const auto modified = fs::last_write_time(marker);
  expect(processor->initialize(nullptr) == kResultOk, "initialize profile-only user");
  expect(!fs::exists(f.data / "plugin"), "profile-only user gets no config write");
  expect(read(marker) == "saved layout and preset data" && fs::last_write_time(marker) == modified,
         "existing profile remains untouched");
  expect(!config(*processor).hasObjectMember("startupView"), "profile-only user retains effects default");
  expect(processor->terminate() == kResultOk, "terminate profile-only component");
}

void writeFailure(const fs::path &root) {
  Fixture f(root, L"write-failure");
  fs::create_directories(f.data / "plugin" / "config.json.tmp");
  auto processor = owned(new EffeTuneProcessor);
  expect(processor->initialize(nullptr) == kResultOk, "failed first-run save does not reject initialize");
  expect(!fs::exists(f.configPath()), "failed save creates no config file");
  expect(config(*processor)["startupView"].getString() == "last", "failed save keeps session last-view default without history");
  expect(processor->terminate() == kResultOk, "terminate failed-save component");
}
} // namespace

int main() {
  testing::suppressCrtModalDialogs();
  try {
    const auto root = soundDataDirectory();
    expect(!root.empty(), "supply an isolated MIV_SOUND_DATA_DIR");
    firstRun(root, false);
    firstRun(root, true);
    rejected(root, L"early-override", L"unexpected-webview-data", false);
    rejected(root, L"late-override", L"unexpected-webview-data", true);
    rejected(root, L"early-empty-override", L"", false);
    rejected(root, L"late-empty-override", L"", true);
    invalidThenRepaired(root);
    existingConfig(root, L"existing-config", "{\n  \"language\":\"ja\", \"columns\":3\n}\n", false);
    existingConfig(root, L"late-config", R"({"startupView":"effects","columns":3})", true);
    existingConfig(root, L"visualizer-config", R"({"startupView":"visualizer"})", false);
    existingConfig(root, L"library-config", R"({"startupView":"library"})", true);
    existingConfig(root, L"last-view-config", R"({"startupView":"last","lastStartupView":"effects"})", false);
    existingProfile(root, false);
    existingProfile(root, true);
    writeFailure(root);
    expect(!cleanupFailed, "restore test environment");
    std::cout << "15 processor startup cases passed (WebView2 is not linked)\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Processor startup failure: " << error.what() << '\n';
    return 1;
  }
}
