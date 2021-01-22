#ifndef QSV2FLV_QSV_READER_H_
#define QSV2FLV_QSV_READER_H_

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "qsv/types.h"

/// \file
/// Reading a `.qsv` container: validate once, then hand out each segment as a
/// plain byte stream of FLV or MPEG-TS.
///
/// No FFmpeg and no Qt here, so this layer can be fuzzed on its own.

namespace qsv {

/// A Reader that has failed stays failed; every later call is a no-op.
enum class Status {
  kOk,
  kCannotOpen,
  kTruncated,
  kBadSignature,
  kUnsupportedVersion,
  kCorrupt,
  kReadFailed,
};

/// One short sentence per Status, for the CLI and the GUI's status column.
const char* Describe(Status status);

/// Fraction of `total` that `processed` is, clamped to [0, 1]; 0 when `total`
/// is not positive.
///
/// Free-standing so the arithmetic can be tested past 2 GiB, which no fixture
/// would write to disk.
double ProgressOf(std::int64_t processed, std::int64_t total);

class Reader {
 public:
  /// Validates in full: status() is final after this returns.
  explicit Reader(const std::string& path);

  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;

  [[nodiscard]] Status status() const { return status_; }
  [[nodiscard]] bool ok() const { return status_ == Status::kOk; }

  /// 1 or 2; 0 if the container never validated.
  [[nodiscard]] std::uint32_t version() const { return version_; }

  [[nodiscard]] int segment_count() const {
    return static_cast<int>(indices_.size());
  }

  /// Sum of every segment size, in bytes. The denominator of progress().
  [[nodiscard]] std::int64_t total_size() const { return total_size_; }

  /// Everything before the current segment plus what has been served from it.
  /// Derived, not accumulated, so seeking backwards reports the truth.
  [[nodiscard]] std::int64_t processed_size() const;

  [[nodiscard]] double progress() const {
    return ProgressOf(processed_size(), total_size_);
  }

  /// Position at the start of segment `index` and decrypt its prefix. False if
  /// the reader has failed, the index is out of range, or the segment is not
  /// where the index table said it was.
  bool SeekToSegment(int index);

  /// Read up to `size` bytes: the count, 0 at end of segment, -1 on error.
  ///
  /// Short reads are normal — the first call after SeekToSegment() is served
  /// from the prefix and stops at its boundary. 0 and -1 are distinct so
  /// remux/ can tell a truncated file from a clean end of stream.
  int Read(std::uint8_t* buffer, int size);

 private:
  /// Latch `status` and return false, so callers can `return Fail(...)`.
  bool Fail(Status status);

  bool ReadExactly(std::uint8_t* buffer, std::int64_t size);
  bool Parse();

  std::ifstream file_;
  std::int64_t file_size_ = 0;
  Status status_ = Status::kOk;
  std::uint32_t version_ = 0;

  std::vector<Index> indices_;
  /// Prefix sums of the segment sizes; one longer than indices_.
  std::vector<std::int64_t> segment_start_;
  std::int64_t total_size_ = 0;

  int segment_ = -1;
  /// Bytes served out of the current segment, prefix included.
  std::int64_t consumed_ = 0;
  /// Bytes of the current segment still unread in the file, prefix excluded.
  std::int64_t remaining_ = 0;

  /// The decrypted prefix of the current segment, buffered whole at seek time.
  /// Decrypting in place on the first read instead would apply the cipher to a
  /// fixed 0x400 bytes of the caller's buffer, however few were read into it.
  std::array<std::uint8_t, kEncryptedPrefixSize> prefix_{};
  std::uint32_t prefix_consumed_ = kEncryptedPrefixSize;
};

}  // namespace qsv

#endif  // QSV2FLV_QSV_READER_H_
