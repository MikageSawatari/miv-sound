#include "bridge/config_store.h"
#include "bridge/data_directory.h"
#include "bridge/preset_store.h"
#include "bridge/sfz_library.h"
#include "bridge/state_codec.h"
#include "plugin/plugin_ids.h"
#include "plugin/version.h"
#include "choc/text/choc_JSON.h"

#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/utility/memoryibstream.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include "../support/crt_dialog_suppression.h"

namespace {
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace effetune::vst;
namespace fs = std::filesystem;

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class Plugin {
public:
  explicit Plugin(const char *path) {
    module_ = LoadLibraryA(path);
    expect(module_ != nullptr, "load mIV Sound DLL (including its spaced filename)");
    const auto init = reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module_, "InitDll"));
    expect(init != nullptr && init(), "initialize DLL");
    factory = reinterpret_cast<GetFactoryProc>(GetProcAddress(module_, "GetPluginFactory"))();
    expect(factory != nullptr, "load factory");
  }
  ~Plugin() {
    factory->release();
    (void)reinterpret_cast<bool (PLUGIN_API *)()>(GetProcAddress(module_, "ExitDll"))();
    FreeLibrary(module_);
  }
  IComponent *create() {
    IComponent *component = nullptr;
    expect(factory->createInstance(plugin::kProcessorId.toTUID(), IComponent::iid.toTUID(),
               reinterpret_cast<void **>(&component)) == kResultOk && component,
           "create mIV Sound component");
    return component;
  }
  IPluginFactory *factory = nullptr;
private:
  HMODULE module_ = nullptr;
};

void testIdentity(Plugin &dll, const char *path) {
  PFactoryInfo info{};
  expect(dll.factory->getFactoryInfo(&info) == kResultOk &&
             std::string(info.vendor) == "mImageViewer" &&
             std::string(info.url) == "https://mikage.to/mimageviewer/", "factory branding");
  IPluginFactory2 *factory2 = nullptr;
  expect(dll.factory->queryInterface(IPluginFactory2::iid, reinterpret_cast<void **>(&factory2)) == kResultOk,
         "query versioned factory");
  PClassInfo2 cls{};
  expect(factory2->getClassInfo2(0, &cls) == kResultOk, "read class info");
  factory2->release();
#if defined(EFFETUNE_AUTOMATION_HOST_GATE_FIXTURE)
  const FUID expected(0x8A41B5DC, 0x7E734D04, 0xB8DB87F7, 0x017CB379);
  const std::string name = "mIV Sound Automation Host Gate";
#else
  const FUID expected(0x1ED3877F, 0xD2CE46D6, 0x896338DD, 0xBBE03E72);
  const std::string name = "mIV Sound";
#endif
  expect(expected == FUID::fromTUID(cls.cid) && std::string(cls.name) == name &&
             std::string(cls.version) == EFFETUNE_PLUGIN_VERSION_STR, "isolated identity and display version");

  DWORD unused = 0;
  std::vector<char> resource(GetFileVersionInfoSizeA(path, &unused));
  expect(!resource.empty() && GetFileVersionInfoA(path, 0, static_cast<DWORD>(resource.size()), resource.data()),
         "read PE version resource");
  VS_FIXEDFILEINFO *version = nullptr;
  UINT bytes = 0;
  expect(VerQueryValueA(resource.data(), "\\", reinterpret_cast<void **>(&version), &bytes) &&
             bytes == sizeof(*version) && version->dwFileVersionMS == MAKELONG(15, 0) &&
             version->dwFileVersionLS == MAKELONG(1, 0) &&
             version->dwProductVersionMS == version->dwFileVersionMS &&
             version->dwProductVersionLS == version->dwFileVersionLS, "PE numeric version is 0.15.0.1");
}

void testInvalidDirectories(Plugin &dll, const fs::path &root) {
  const auto previousDirectory = fs::current_path();
  const auto sandbox = root / "invalid";
  fs::create_directories(sandbox);
  fs::current_path(sandbox);
  const auto appData = sandbox / "appdata";
  const auto home = sandbox / "home";
  SetEnvironmentVariableW(L"APPDATA", appData.c_str());
  SetEnvironmentVariableW(L"HOME", home.c_str());
  SetEnvironmentVariableW(L"USERPROFILE", home.c_str());
  for (const wchar_t *value : {static_cast<const wchar_t *>(nullptr), L"", L"relative", L"C:relative", L"\\root-relative"}) {
    SetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", value);
    ConfigStore config;
    PresetStore presets;
    std::string content;
    expect(config.path().empty() && presets.presetPath().empty(), "invalid storage is retained as empty");
    expect(!config.load(content) && !config.save("{}") &&
               !presets.exists("effetune_presets.json") &&
               !presets.read("effetune_plugin_presets.json", content) &&
               !presets.write("effetune_plugin_presets.json", "{}"), "invalid stores refuse all I/O");
    auto *component = dll.create();
    // Changing the environment after construction cannot repair invalid stores.
    SetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", root.c_str());
    expect(component->initialize(nullptr) == kInvalidArgument, "invalid storage fails initialize");
    component->release();
    expect(fs::is_empty(sandbox), "failed startup writes nothing to cwd, APPDATA or home");
  }
  fs::current_path(previousDirectory);
}

void testStores(const fs::path &root) {
  SetEnvironmentVariableW(L"MIV_SOUND_DATA_DIR", root.c_str());
  ConfigStore config;
  PresetStore presets;
  expect(config.path() == root / "plugin" / "config.json" &&
             presets.presetPath() == root / "plugin" / "effetune_presets.json", "portable storage paths");
  expect(config.save("{\"theme\":\"dark\"}") &&
             presets.write("effetune_presets.json", "{\"custom\":[]}") &&
             presets.write("effetune_plugin_presets.json", "{\"VolumePlugin\":{}}"), "write both preset stores and config");
  expect(fs::is_regular_file(root / "plugin" / "effetune_plugin_presets.json"), "plugin presets share the new directory");
  SfzLibrary sfz(config.path().parent_path() / "sfz-references.json");
  expect(choc::json::parse(sfz.request("list"))["ok"].getBool() &&
             fs::is_regular_file(root / "plugin" / "sfz-references.json.lock"),
         "SFZ references follow the config parent folder");
}

void testLegacyState(Plugin &dll, const char *fixture) {
  std::ifstream input(fixture, std::ios::binary);
  std::string json{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  expect(!json.empty(), "read synthetic shipped mIV state file");
  for (const auto &oldVersion : {std::string("0.14.0"), std::string("0.11.1")}) {
    auto document = json;
    document.replace(document.find("0.14.0"), 6, oldVersion);
    auto *component = dll.create();
    expect(component->initialize(nullptr) == kResultOk, "valid directory allows startup");
    ResizableMemoryIBStream state;
    expect(state.write(document.data(), static_cast<int32>(document.size()), nullptr) == kResultTrue, "load raw JSON without base64");
    state.rewind();
    expect(component->setState(&state) == kResultOk, "accept shipped formatVersion 1 state");
    ResizableMemoryIBStream saved;
    expect(component->getState(&saved) == kResultOk, "save restored pipeline");
    PluginStateDocument decoded;
    expect(StateCodec::decode(std::string_view(static_cast<const char *>(saved.getData()), saved.getCursor()), decoded), "decode round trip");
    expect(decoded.appVersion == oldVersion && decoded.pipelineA.plugins.size() == 2 &&
               decoded.pipelineA.plugins[0].id == 7 && decoded.pipelineA.plugins[0].extraJson.find("VolumePlugin") != std::string::npos &&
               decoded.pipelineA.plugins[0].enabled && decoded.pipelineA.plugins[0].parametersJson.find("-6") != std::string::npos &&
               decoded.pipelineA.plugins[1].id == 9 && !decoded.pipelineA.plugins[1].enabled &&
               !decoded.pipelineBInitialized && decoded.currentPipeline == 'A' && !decoded.masterBypass &&
               decoded.ui.columns == 2 && decoded.ui.zoom == 1.25 && decoded.oversampling.factor == 2 &&
               decoded.oversampling.phase == OversamplingPhase::minimum && decoded.oversampling.quality == FilterQuality::high &&
               decoded.automation.logicalPluginIdWatermark == 12 && decoded.automation.bindings.size() == 1 &&
               decoded.automation.bindings[0].slot == 3 && decoded.automation.bindings[0].pluginId == 12 &&
               decoded.automation.bindings[0].lifecycle == AutomationBindingLifecycle::tombstone,
           "restore pipelines, parameters, bypass, UI, oversampling and automation from shipped mIV state");
    saved.rewind();
    expect(component->setState(&saved) == kResultOk, "saved state is accepted again");
    expect(component->terminate() == kResultOk, "terminate restored component");
    component->release();
  }
}
} // namespace

int main(int argc, char **argv) {
  effetune::vst::testing::suppressCrtModalDialogs();
  try {
    expect(argc == 3, "expected DLL and fixture paths");
    const auto root = soundDataDirectory();
    expect(!root.empty(), "CTest supplies an isolated test directory");
    Plugin dll(argv[1]);
    testIdentity(dll, argv[1]);
    testInvalidDirectories(dll, root);
    testStores(root);
    testLegacyState(dll, argv[2]);
    std::cout << "mIV Sound identity, storage isolation and shipped-state compatibility verified\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
