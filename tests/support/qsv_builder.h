#ifndef QSV2FLV_TESTS_SUPPORT_QSV_BUILDER_H_
#define QSV2FLV_TESTS_SUPPORT_QSV_BUILDER_H_

#include <cstdint>
#include <string>
#include <vector>

/// \file
/// Synthesising `.qsv` containers, so the suite needs none of the real ones.
///
/// Version 1 is fully constructible: the index table is in the clear and the
/// segment prefix is XORed, which is self-inverse. Version 2 is not — see
/// qsv::DecryptV2() and docs/Testing.md.

namespace test {

/// Builds one container in memory. Valid by default; each setter breaks
/// exactly one thing, which is how each qsv::Status arm gets a fixture.
class QsvBuilder {
 public:
  explicit QsvBuilder(std::uint32_t version = 1);

  /// Append a segment whose decrypted contents are `payload`. Short payloads
  /// are zero-padded to qsv::kEncryptedPrefixSize, which the format requires.
  void AddSegment(std::vector<std::uint8_t> payload);

  // --- Deliberate damage, one setter per Reader failure path ---

  /// Overwrite the magic. Anything but "QIYI VIDEO" must be rejected.
  void SetSignature(const std::string& signature);

  /// Set the header's version without changing how the body is encoded: the
  /// unsupported-version path, and a structurally valid version 2 that cannot
  /// decode.
  void SetDeclaredVersion(std::uint32_t version);

  /// Write an index count that disagrees with the number of segments added.
  void SetDeclaredIndexCount(std::uint32_t count);

  void SetSegmentOffset(std::size_t segment, std::uint64_t offset);
  void SetSegmentSize(std::size_t segment, std::uint32_t size);

  /// Cut the finished file short, for the truncation paths.
  void TruncateTo(std::size_t bytes);

  [[nodiscard]] std::vector<std::uint8_t> Build() const;

  /// What segment `index` should read back as — the expected value in a
  /// round-trip assertion.
  [[nodiscard]] const std::vector<std::uint8_t>& Payload(
      std::size_t index) const;

  [[nodiscard]] std::size_t segment_count() const { return segments_.size(); }

 private:
  std::uint32_t encoded_version_;
  std::uint32_t declared_version_;
  bool version_overridden_ = false;

  std::string signature_;
  std::uint32_t declared_index_count_ = 0;
  bool index_count_overridden_ = false;
  std::size_t truncate_to_ = 0;
  bool truncated_ = false;

  struct Segment {
    std::vector<std::uint8_t> payload;
    std::uint64_t offset_override = 0;
    bool offset_overridden = false;
    std::uint32_t size_override = 0;
    bool size_overridden = false;
  };
  std::vector<Segment> segments_;
};

/// `count` bytes from a fixed sequence. A multiply-xor counter rather than
/// <random>, whose distributions are not portable — a fixture that differed
/// between libstdc++ and libc++ could not be reproduced locally.
std::vector<std::uint8_t> PseudoRandomBytes(std::size_t count,
                                            std::uint32_t seed);

}  // namespace test

#endif  // QSV2FLV_TESTS_SUPPORT_QSV_BUILDER_H_
