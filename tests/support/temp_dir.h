#ifndef QSV2FLV_TESTS_SUPPORT_TEMP_DIR_H_
#define QSV2FLV_TESTS_SUPPORT_TEMP_DIR_H_

#include <cstdint>
#include <string>
#include <vector>

/// \file
/// A scratch directory that removes itself.

namespace test {

/// One per test executable, created under the system temp directory and
/// deleted with everything in it when the object dies.
///
/// The name carries the process id, so two suites running concurrently under
/// `ctest -j` cannot collide — and a suite that crashes leaves exactly one
/// identifiable directory behind rather than overwriting a shared one.
class TempDir {
 public:
  explicit TempDir(const std::string& tag);
  ~TempDir();
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::string& path() const { return path_; }

  /// Write `bytes` to `<path>/<name>` and return the full path.
  std::string WriteFile(const std::string& name,
                        const std::vector<std::uint8_t>& bytes) const;

  /// Full path to `name`, whether or not it exists yet.
  [[nodiscard]] std::string FilePath(const std::string& name) const;

 private:
  std::string path_;
};

/// Size of `path` in bytes, or -1 if it is not there.
std::int64_t FileSize(const std::string& path);

}  // namespace test

#endif  // QSV2FLV_TESTS_SUPPORT_TEMP_DIR_H_
