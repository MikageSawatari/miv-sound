#include "bridge/config_store.h"

#include "bridge/data_directory.h"

#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace effetune::vst {

ConfigStore::ConfigStore() : path_(resolvePath()) {}

ConfigStore::ConfigStore(std::filesystem::path path) : path_(std::move(path)) {}

void ConfigStore::setError(std::string *destination, std::string value) {
  if (destination != nullptr) {
    *destination = std::move(value);
  }
}

std::filesystem::path ConfigStore::resolvePath() {
  const auto directory = soundDataDirectory();
  return directory.empty() ? std::filesystem::path{}
                           : directory / "plugin" / "config.json";
}

bool ConfigStore::load(std::string &content, std::string *error) const {
  content.clear();
  if (path_.empty()) {
    setError(error, kDataDirectoryError);
    return false;
  }
  std::error_code filesystemError;
  if (!std::filesystem::exists(path_, filesystemError)) {
    content = "{}";
    return !filesystemError;
  }
  std::ifstream input(path_, std::ios::binary);
  if (!input) {
    setError(error, "Unable to open UI config");
    return false;
  }
  content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
  if ((!input.good() && !input.eof()) || content.size() > 64u * 1024u) {
    setError(error, "Unable to read UI config");
    return false;
  }
  return true;
}

bool ConfigStore::save(const std::string_view content, std::string *error) const {
  if (path_.empty()) {
    setError(error, kDataDirectoryError);
    return false;
  }
  if (content.size() > 64u * 1024u) {
    setError(error, "UI config is too large");
    return false;
  }
  std::error_code filesystemError;
  std::filesystem::create_directories(path_.parent_path(), filesystemError);
  if (filesystemError) {
    setError(error, "Unable to create UI config directory");
    return false;
  }
  auto temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) {
      setError(error, "Unable to write temporary UI config");
      return false;
    }
  }
#if defined(_WIN32)
  if (MoveFileExW(temporary.c_str(), path_.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    std::filesystem::remove(temporary, filesystemError);
    setError(error, "Unable to atomically replace UI config");
    return false;
  }
#else
  std::filesystem::rename(temporary, path_, filesystemError);
  if (filesystemError) {
    std::filesystem::remove(temporary, filesystemError);
    setError(error, "Unable to atomically replace UI config");
    return false;
  }
#endif
  return true;
}

} // namespace effetune::vst
