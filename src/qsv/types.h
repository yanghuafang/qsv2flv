#ifndef QSV2FLV_QSV_TYPES_H_
#define QSV2FLV_QSV_TYPES_H_

#include <cstddef>
#include <cstdint>

/// \file
/// The on-disk shape of a `.qsv` container. docs/QsvFormat.md is the prose
/// version.
///
/// Fields are decoded byte by byte rather than read as a packed struct: that
/// would depend on `#pragma pack(1)` and on a little-endian host, neither of
/// which is visible at the read site.

namespace qsv {

/// ASCII magic at offset 0, stored without a terminator — hence
/// kSignatureSize rather than sizeof(), which would count the NUL.
inline constexpr char kSignature[] = "QIYI VIDEO";
inline constexpr std::size_t kSignatureSize = 10;

/// Bytes from the start of the file to the index-flag bitmap.
inline constexpr std::size_t kHeaderSize = 0x5A;

/// One index record, encrypted on disk when the container is version 2.
inline constexpr std::size_t kIndexSize = 0x1C;

/// Only the first 1 KiB of a segment is encrypted; the rest is FLV or
/// MPEG-TS in the clear.
inline constexpr std::uint32_t kEncryptedPrefixSize = 0x400;

/// A real file holds tens of segments. This exists so a corrupt length cannot
/// ask for a multi-gigabyte allocation before anything is validated.
inline constexpr std::uint32_t kMaxIndexCount = 0xFFFF;

/// The header fields this project uses. The `_unknown` runs in
/// docs/QsvFormat.md are skipped rather than stored.
struct Header {
  std::uint32_t version = 0;
  std::uint64_t xml_offset = 0;
  std::uint32_t xml_size = 0;
  std::uint32_t index_count = 0;
};

/// Where one segment lives. The 16-byte `_codetable` preceding these on disk
/// is consumed by DecryptV2() and not otherwise used.
struct Index {
  std::uint64_t segment_offset = 0;
  std::uint32_t segment_size = 0;
};

/// Little-endian scalar reads. `bytes` must hold at least 4 (resp. 8) bytes;
/// callers check their own buffer first.
std::uint32_t ReadLe32(const std::uint8_t* bytes);
std::uint64_t ReadLe64(const std::uint8_t* bytes);

/// True when the first kSignatureSize bytes are the "QIYI VIDEO" magic.
bool HasSignature(const std::uint8_t* raw);

/// Decode the fixed header. No validation beyond the layout — which versions
/// and counts are acceptable is Reader's policy.
Header ParseHeader(const std::uint8_t* raw);

/// Decrypt (version 2 only) and decode one index record in place.
///
/// `raw` is modified: version 2 has no separate plaintext to decode from.
Index DecodeIndex(std::uint8_t* raw, std::uint32_t version);

}  // namespace qsv

#endif  // QSV2FLV_QSV_TYPES_H_
