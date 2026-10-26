#include "bridge/config_store.h"
#include "bridge/data_directory.h"
#include "bridge/preset_store.h"
#include "bridge/sfz_library.h"
#include "choc/memory/choc_Base64.h"
#include "choc/text/choc_JSON.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace fs = std::filesystem;
using namespace effetune::vst;
using choc::value::Value;
using choc::value::ValueView;

void expect(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

std::string read(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  expect(static_cast<bool>(input), "open " + path.string());
  return {std::istreambuf_iterator<char>(input), {}};
}

void write(const fs::path &path, const std::string &text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  expect(static_cast<bool>(output), "write synthetic legacy data");
}

std::string utf8(const fs::path &path) {
  const auto text = path.generic_u8string();
  return {reinterpret_cast<const char *>(text.data()), text.size()};
}

fs::path fromUtf8(const std::string &text) {
  return fs::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
}

bool sameJson(ValueView a, ValueView b) {
  if (a.isObject()) {
    if (!b.isObject() || a.size() != b.size()) return false;
    for (std::uint32_t index = 0; index < a.size(); ++index) {
      const auto member = a.getObjectMemberAt(index);
      if (!b.hasObjectMember(member.name) || !sameJson(member.value, b[member.name])) return false;
    }
    return true;
  }
  if (a.isArray()) {
    if (!b.isArray() || a.size() != b.size()) return false;
    for (std::uint32_t index = 0; index < a.size(); ++index)
      if (!sameJson(a[index], b[index])) return false;
    return true;
  }
  return choc::json::toString(a) == choc::json::toString(b);
}

void copyUnchanged(const fs::path &source, const fs::path &destination) {
  fs::create_directories(destination.parent_path());
  fs::copy_file(source, destination, fs::copy_options::overwrite_existing);
  expect(read(source) == read(destination), "migration copies bytes without changing the legacy JSON");
}

void checkVersion(const fs::path &fixtures, const fs::path &root, const std::string &version) {
  const auto plugin = root / "plugin";
  const auto legacy = root / "legacy" / version / "Frieve/EffeTunePlugin";
  fs::create_directories(plugin);
  write(legacy / "config.json", read(fixtures / version / "config.json"));
  copyUnchanged(legacy / "config.json", plugin / "config.json");
  ConfigStore config;
  expect(config.path() == plugin / "config.json", "default config resolves under MIV_SOUND_DATA_DIR/plugin");
  std::string content, error;
  const auto oldConfig = read(legacy / "config.json");
  expect(config.load(content, &error) && content == oldConfig, "load copied legacy config: " + error);
  expect(choc::json::parse(content).isObject(), "legacy config is a JSON object");
  expect(config.save(content, &error) && read(config.path()) == oldConfig, "config resave retains every byte and key");
  ConfigStore reopenedConfig;
  expect(reopenedConfig.load(content, &error) && content == oldConfig, "config survives reopening");

  PresetStore presets;
  expect(presets.presetPath() == plugin / "effetune_presets.json", "default presets resolve under the copied folder");
  // These serializers have the same shape in all four compared versions.
  for (const auto *name : {"effetune_presets.json", "effetune_plugin_presets.json"}) {
    const auto original = read(fixtures / "v0.14.0" / name);
    write(legacy / name, original);
    copyUnchanged(legacy / name, plugin / name);
    expect(presets.read(name, content, &error) && content == original, "load copied legacy preset table");
    expect(choc::json::parse(content).isObject(), "legacy preset table is an object");
    expect(presets.write(name, content, &error), "save the copied preset table");
    PresetStore reopenedPresets;
    expect(reopenedPresets.read(name, content, &error) && content == original, "preset resave retains all keys and values");
    expect(read(legacy / name) == original, "resaving never changes the migration source");
  }

  const auto registry = config.path().parent_path() / "sfz-references.json";
  if (version == "v0.12.0") {
    fs::remove(registry); // only this test's disposable, known filename
    SfzLibrary sfz(registry);
    const auto listed = choc::json::parse(sfz.request("list"));
    expect(listed["ok"].getBool() && listed["data"].size() == 0 && !fs::exists(registry),
           "v0.12 has no native SFZ registry; list accepts its absence without inventing JSON");
  } else {
    const auto sfzVersion = version == "v0.13.0" ? version : "v0.14.0";
    const auto fixtureEntries = choc::json::parse(read(fixtures / sfzVersion / "sfz-references.json"));
    auto expected = choc::value::createEmptyArray();
    const auto bankRoot = fs::absolute(root / "synthetic SFZ sources").lexically_normal();
    // Materialise synthetic absolute paths BEFORE staging the old configuration.
    // The actual legacy -> plugin copy below changes no contents at all.
    for (std::uint32_t index = 0; index < fixtureEntries.size(); ++index) {
      auto entry = Value(fixtureEntries[index]);
      const auto templatePath = entry["path"].get<std::string>();
      expect(templatePath.starts_with("@SFZ_ROOT@/"), "fixture uses a controlled synthetic source root");
      const auto relative = templatePath.substr(std::string("@SFZ_ROOT@/").size());
      entry.setMember("root", utf8(bankRoot));
      entry.setMember("path", utf8(bankRoot / relative));
      write(bankRoot / relative, "<region> sample=*sine\n");
      expected.addArrayElement(std::move(entry));
    }
    write(bankRoot / "Example.bank.xml", "<AriaBank/>\n");
    const auto original = choc::json::toString(expected);
    write(legacy / "sfz-references.json", original);
    copyUnchanged(legacy / "sfz-references.json", registry);
    SfzLibrary sfz(registry);
    auto listed = choc::json::parse(sfz.request("list"));
    expect(listed["ok"].getBool() && listed["data"].size() == expected.size(), "load every copied SFZ reference");
    expect(read(registry) == original, "a read-only SFZ list does not rewrite the migrated bytes");
    auto normalized = choc::value::createEmptyArray();
    for (std::uint32_t index = 0; index < expected.size(); ++index) {
      auto entry = Value(expected[index]);
      const auto publicEntry = listed["data"][index];
      expect(publicEntry["id"].getString() == entry["id"].getString() &&
                 publicEntry["name"].getString() == entry["name"].getString(), "SFZ identifiers and names survive copying");
      const auto relative = utf8(fromUtf8(entry["path"].get<std::string>()).lexically_relative(bankRoot));
      expect(publicEntry["selectedPath"].getString() == relative, "public SFZ paths remain relative to their original root");
      const auto banks = entry["bankPaths"].isVoid() ? choc::value::createEmptyArray() : Value(entry["bankPaths"]);
      expect(sameJson(publicEntry["bankPaths"], banks), "UI receives the same bank definition paths");
      expect(entry["importedAt"].isVoid() ? publicEntry["importedAt"].isVoid()
                                         : sameJson(entry["importedAt"], publicEntry["importedAt"]),
             "UI import timestamp is optional in v0.13 and preserved in v0.14");
      auto request = choc::value::createObject({});
      request.addMember("id", entry["id"]);
      request.addMember("relativePath", relative);
      request.addMember("maxBytes", std::int64_t{1024});
      const auto opened = choc::json::parse(sfz.request("openRead", choc::json::toString(request)));
      expect(opened["ok"].getBool(), "copied SFZ references still read the original source file");
      auto chunk = choc::value::createObject({});
      chunk.addMember("readId", opened["data"]["readId"]);
      chunk.addMember("offset", std::int64_t{0});
      chunk.addMember("length", opened["data"]["size"]);
      const auto bytes = choc::json::parse(sfz.request("readChunk", choc::json::toString(chunk)));
      std::string decoded;
      expect(bytes["ok"].getBool() && choc::base64::decodeToContainer(decoded, bytes["data"]["base64"].getString()) &&
                 decoded == read(bankRoot / relative), "read the same SFZ bytes via the retained reference ID");
      expect(choc::json::parse(sfz.request("closeRead", choc::json::toString(chunk)))["ok"].getBool(), "close SFZ read");
      if (entry["bankPaths"].isVoid()) entry.addMember("bankPaths", choc::value::createEmptyArray());
      if (entry["importedAt"].isVoid()) entry.addMember("importedAt", "");
      normalized.addArrayElement(std::move(entry));
    }
    // Removing an absent valid ID exercises the production load + save path
    // without changing any reference or refreshing timestamps by re-selecting.
    expect(choc::json::parse(sfz.request("remove", R"({"id":"ffffffffffffffffffffffff"})"))["ok"].getBool(), "resave all retained SFZ references");
    expect(sameJson(normalized, choc::json::parse(read(registry))), "SFZ resave preserves old fields and only fills absent optional metadata");
    SfzLibrary reopenedSfz(registry);
    expect(sameJson(listed, choc::json::parse(reopenedSfz.request("list"))), "SFZ public metadata survives reopening");
    expect(read(legacy / "sfz-references.json") == original, "SFZ migration source is never rewritten");
  }
  expect(read(legacy / "config.json") == oldConfig, "the old config is untouched");
  std::cout << version << " copied JSON storage roundtrip passed\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    expect(argc == 2, "expected the synthetic fixture directory");
    const auto root = soundDataDirectory().lexically_normal();
    expect(root.is_absolute() && root.filename() == "effetune_json_storage_compatibility_tests",
           "use only this CTest's disposable MIV_SOUND_DATA_DIR");
    for (const auto *version : {"v0.12.0", "v0.13.0", "v0.14.0", "v0.15.0"}) checkVersion(argv[1], root, version);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "JSON storage compatibility test failed: " << error.what() << '\n';
    return 1;
  }
}
