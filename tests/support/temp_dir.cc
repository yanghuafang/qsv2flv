#include "support/temp_dir.h"

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <system_error>

namespace test {

TempDir::TempDir(const std::string& tag) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      ("qsv2flv-" + tag + "-" + std::to_string(getpid()));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  path_ = root.string();
}

TempDir::~TempDir() {
  // Errors are swallowed on purpose: a destructor that throws during stack
  // unwinding would replace a real test failure with a terminate().
  std::error_code ignored;
  std::filesystem::remove_all(path_, ignored);
}

std::string TempDir::FilePath(const std::string& name) const {
  return (std::filesystem::path(path_) / name).string();
}

std::string TempDir::WriteFile(const std::string& name,
                               const std::vector<std::uint8_t>& bytes) const {
  const std::string full = FilePath(name);
  std::ofstream out(full, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return full;
}

std::int64_t FileSize(const std::string& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  return error ? -1 : static_cast<std::int64_t>(size);
}

}  // namespace test
