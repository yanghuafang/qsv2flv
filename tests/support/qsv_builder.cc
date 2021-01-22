#include "support/qsv_builder.h"

#include <algorithm>
#include <cstring>

#include "qsv/crypto.h"
#include "qsv/types.h"

namespace test {
namespace {

void AppendLe32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  for (int i = 0; i < 4; ++i) {
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
  }
}

void AppendLe64(std::vector<std::uint8_t>& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
  }
}

void AppendZeros(std::vector<std::uint8_t>& out, std::size_t count) {
  out.insert(out.end(), count, 0);
}

}  // namespace

QsvBuilder::QsvBuilder(std::uint32_t version)
    : encoded_version_(version),
      declared_version_(version),
      signature_(qsv::kSignature, qsv::kSignatureSize) {}

void QsvBuilder::AddSegment(std::vector<std::uint8_t> payload) {
  if (payload.size() < qsv::kEncryptedPrefixSize) {
    payload.resize(qsv::kEncryptedPrefixSize, 0);
  }
  segments_.push_back(Segment{std::move(payload), 0, false, 0, false});
}

void QsvBuilder::SetSignature(const std::string& signature) {
  signature_ = signature;
  signature_.resize(qsv::kSignatureSize, '\0');
}

void QsvBuilder::SetDeclaredVersion(std::uint32_t version) {
  declared_version_ = version;
  version_overridden_ = true;
}

void QsvBuilder::SetDeclaredIndexCount(std::uint32_t count) {
  declared_index_count_ = count;
  index_count_overridden_ = true;
}

void QsvBuilder::SetSegmentOffset(std::size_t segment, std::uint64_t offset) {
  segments_[segment].offset_override = offset;
  segments_[segment].offset_overridden = true;
}

void QsvBuilder::SetSegmentSize(std::size_t segment, std::uint32_t size) {
  segments_[segment].size_override = size;
  segments_[segment].size_overridden = true;
}

void QsvBuilder::TruncateTo(std::size_t bytes) {
  truncate_to_ = bytes;
  truncated_ = true;
}

const std::vector<std::uint8_t>& QsvBuilder::Payload(std::size_t index) const {
  return segments_[index].payload;
}

std::vector<std::uint8_t> QsvBuilder::Build() const {
  const std::uint32_t index_count =
      index_count_overridden_ ? declared_index_count_
                              : static_cast<std::uint32_t>(segments_.size());
  const std::size_t flag_size = (segments_.size() + 7) / 8;
  const std::size_t body_start =
      qsv::kHeaderSize + flag_size + segments_.size() * qsv::kIndexSize;

  // Segment offsets are laid out first, because the index records that carry
  // them are written before the segments themselves.
  std::vector<std::uint64_t> offsets;
  std::size_t cursor = body_start;
  for (const Segment& segment : segments_) {
    offsets.push_back(cursor);
    cursor += segment.payload.size();
  }

  std::vector<std::uint8_t> out;
  out.reserve(cursor);

  // --- Header (docs/QsvFormat.md) ---
  out.insert(out.end(), signature_.begin(), signature_.end());
  AppendLe32(out, version_overridden_ ? declared_version_ : encoded_version_);
  AppendZeros(out, 0x10);  // vid
  AppendLe32(out, 1);      // _unknown1, always 1 in real files
  AppendZeros(out, 0x20);  // _unknown2
  AppendLe32(out, 0);      // _unknown3
  AppendLe32(out, 0);      // _unknown4
  AppendLe64(out, 0);      // xml_offset — nothing here reads the XML section
  AppendLe32(out, 0);      // xml_size
  AppendLe32(out, index_count);

  // --- Index flag bitmap, purpose unknown, one bit per segment ---
  AppendZeros(out, flag_size);

  // --- Index table ---
  //
  // Written in the clear. That is correct for version 1 and wrong for version
  // 2, which stores each record under DecryptV2() — and is why a version 2
  // container from this builder is only good as a rejection fixture. There is
  // no encryptV2 to call here: see qsv::DecryptV2().
  for (std::size_t i = 0; i < segments_.size(); ++i) {
    const Segment& segment = segments_[i];
    AppendZeros(out, 0x10);  // _codetable
    AppendLe64(
        out, segment.offset_overridden ? segment.offset_override : offsets[i]);
    AppendLe32(out, segment.size_overridden
                        ? segment.size_override
                        : static_cast<std::uint32_t>(segment.payload.size()));
  }

  // --- Segments ---
  for (const Segment& segment : segments_) {
    const std::size_t before = out.size();
    out.insert(out.end(), segment.payload.begin(), segment.payload.end());
    if (encoded_version_ == 1) {
      // DecryptV1 is an XOR, so applying it to plaintext produces the
      // ciphertext the reader will turn back into that plaintext.
      qsv::DecryptV1(out.data() + before, qsv::kEncryptedPrefixSize);
    }
  }

  if (truncated_ && truncate_to_ < out.size()) {
    out.resize(truncate_to_);
  }
  return out;
}

std::vector<std::uint8_t> PseudoRandomBytes(std::size_t count,
                                            std::uint32_t seed) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(count);
  std::uint32_t state = seed | 1u;
  for (std::size_t i = 0; i < count; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    bytes.push_back(static_cast<std::uint8_t>(state >> 24));
  }
  return bytes;
}

}  // namespace test
