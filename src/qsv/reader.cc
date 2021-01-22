#include "qsv/reader.h"

#include <algorithm>
#include <vector>

#include "qsv/crypto.h"

namespace qsv {

const char* Describe(Status status) {
  switch (status) {
    case Status::kOk:
      return "OK";
    case Status::kCannotOpen:
      return "Cannot open the file";
    case Status::kTruncated:
      return "The file ends before the container does";
    case Status::kBadSignature:
      return "Not a QSV file (no QIYI VIDEO signature)";
    case Status::kUnsupportedVersion:
      return "Unsupported QSV version";
    case Status::kCorrupt:
      return "The QSV index table does not describe this file";
    case Status::kReadFailed:
      return "Read error";
  }
  return "Unknown error";
}

double ProgressOf(std::int64_t processed, std::int64_t total) {
  if (total <= 0) {
    return 0.0;
  }
  const double fraction =
      static_cast<double>(processed) / static_cast<double>(total);
  return std::clamp(fraction, 0.0, 1.0);
}

Reader::Reader(const std::string& path) {
  file_.open(path, std::ios::binary | std::ios::ate);
  if (!file_) {
    Fail(Status::kCannotOpen);
    return;
  }

  file_size_ = static_cast<std::int64_t>(file_.tellg());
  file_.seekg(0, std::ios::beg);
  if (file_size_ < 0 || !file_) {
    Fail(Status::kCannotOpen);
    return;
  }

  Parse();
}

bool Reader::Fail(Status status) {
  status_ = status;
  return false;
}

bool Reader::ReadExactly(std::uint8_t* buffer, std::int64_t size) {
  file_.read(reinterpret_cast<char*>(buffer), size);
  return file_.gcount() == size;
}

bool Reader::Parse() {
  std::uint8_t raw_header[kHeaderSize];
  if (!ReadExactly(raw_header, kHeaderSize)) {
    return Fail(Status::kTruncated);
  }
  if (!HasSignature(raw_header)) {
    return Fail(Status::kBadSignature);
  }

  const Header header = ParseHeader(raw_header);
  version_ = header.version;
  if (version_ != 1 && version_ != 2) {
    // The original set an error code here and carried on parsing, so an
    // unrecognised version still allocated and decrypted an index table under
    // rules that did not apply to it. It only ever looked harmless because the
    // caller checked the code straight after construction.
    return Fail(Status::kUnsupportedVersion);
  }
  if (header.index_count < 1 || header.index_count > kMaxIndexCount) {
    return Fail(Status::kCorrupt);
  }

  // One bit per segment, purpose unknown (docs/QsvFormat.md), rounded up to a
  // whole byte. Skipped by seeking rather than reading.
  const std::int64_t flag_size = (header.index_count + 7) / 8;
  const std::int64_t table_size =
      static_cast<std::int64_t>(header.index_count) * kIndexSize;
  if (static_cast<std::int64_t>(kHeaderSize) + flag_size + table_size >
      file_size_) {
    return Fail(Status::kTruncated);
  }
  file_.seekg(flag_size, std::ios::cur);

  std::vector<std::uint8_t> table(static_cast<std::size_t>(table_size));
  if (!ReadExactly(table.data(), table_size)) {
    return Fail(Status::kTruncated);
  }

  indices_.reserve(header.index_count);
  segment_start_.reserve(header.index_count + 1);
  for (std::uint32_t i = 0; i < header.index_count; ++i) {
    const Index index = DecodeIndex(table.data() + (i * kIndexSize), version_);

    // A segment shorter than the encrypted prefix cannot exist: the prefix is
    // the fixed part of every segment.
    if (index.segment_size < kEncryptedPrefixSize) {
      return Fail(Status::kCorrupt);
    }
    // Subtract rather than add. `offset + size > file_size` is the original's
    // check and wraps for an offset near the top of the 64-bit range, which is
    // exactly what a corrupt or hostile index table produces: the sum comes out
    // small, the record passes, and the seek that follows is unbounded.
    const auto file_size = static_cast<std::uint64_t>(file_size_);
    if (index.segment_offset > file_size ||
        index.segment_size > file_size - index.segment_offset) {
      return Fail(Status::kCorrupt);
    }

    segment_start_.push_back(total_size_);
    total_size_ += index.segment_size;
    indices_.push_back(index);
  }
  segment_start_.push_back(total_size_);

  return true;
}

std::int64_t Reader::processed_size() const {
  if (segment_ < 0) {
    return 0;
  }
  return segment_start_[static_cast<std::size_t>(segment_)] + consumed_;
}

bool Reader::SeekToSegment(int index) {
  if (!ok()) {
    return false;
  }
  if (index < 0 || index >= segment_count()) {
    return false;
  }

  const Index& entry = indices_[static_cast<std::size_t>(index)];
  file_.clear();
  file_.seekg(static_cast<std::streamoff>(entry.segment_offset), std::ios::beg);
  if (!file_) {
    return Fail(Status::kCorrupt);
  }
  if (!ReadExactly(prefix_.data(), kEncryptedPrefixSize)) {
    return Fail(Status::kTruncated);
  }

  if (version_ == 1) {
    DecryptV1(prefix_.data(), kEncryptedPrefixSize);
  } else {
    DecryptV2(prefix_.data(), kEncryptedPrefixSize);
  }

  segment_ = index;
  prefix_consumed_ = 0;
  consumed_ = 0;
  remaining_ =
      static_cast<std::int64_t>(entry.segment_size) - kEncryptedPrefixSize;
  return true;
}

int Reader::Read(std::uint8_t* buffer, int size) {
  if (!ok() || segment_ < 0) {
    return -1;
  }
  if (size <= 0) {
    return 0;
  }

  // The decrypted prefix first, and only the prefix: a call that reaches its
  // end returns short rather than topping up from the file. Every consumer here
  // is a stream reader that handles a short read, and splicing the two sources
  // in one call would put the buffered and the unbuffered path in the same
  // arithmetic.
  if (prefix_consumed_ < kEncryptedPrefixSize) {
    const int count = std::min(
        size, static_cast<int>(kEncryptedPrefixSize - prefix_consumed_));
    std::copy_n(prefix_.begin() + prefix_consumed_, count, buffer);
    prefix_consumed_ += static_cast<std::uint32_t>(count);
    consumed_ += count;
    return count;
  }

  if (remaining_ == 0) {
    return 0;
  }

  const int want = static_cast<int>(std::min<std::int64_t>(size, remaining_));
  file_.read(reinterpret_cast<char*>(buffer), want);
  const std::int64_t got = file_.gcount();
  if (got <= 0) {
    Fail(Status::kReadFailed);
    return -1;
  }

  remaining_ -= got;
  consumed_ += got;
  return static_cast<int>(got);
}

}  // namespace qsv
