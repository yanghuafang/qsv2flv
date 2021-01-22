#include "qsv/types.h"

#include <cstring>

#include "qsv/crypto.h"

namespace qsv {

std::uint32_t ReadLe32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8) |
         (static_cast<std::uint32_t>(bytes[2]) << 16) |
         (static_cast<std::uint32_t>(bytes[3]) << 24);
}

std::uint64_t ReadLe64(const std::uint8_t* bytes) {
  return static_cast<std::uint64_t>(ReadLe32(bytes)) |
         (static_cast<std::uint64_t>(ReadLe32(bytes + 4)) << 32);
}

bool HasSignature(const std::uint8_t* raw) {
  return std::memcmp(raw, kSignature, kSignatureSize) == 0;
}

Header ParseHeader(const std::uint8_t* raw) {
  // Offsets from docs/QsvFormat.md. Spelled as literals rather than as a
  // running cursor so each one can be checked against that table by eye.
  Header header;
  header.version = ReadLe32(raw + 0x0A);
  header.xml_offset = ReadLe64(raw + 0x4A);
  header.xml_size = ReadLe32(raw + 0x52);
  header.index_count = ReadLe32(raw + 0x56);
  return header;
}

Index DecodeIndex(std::uint8_t* raw, std::uint32_t version) {
  if (version == 2) {
    DecryptV2(raw, static_cast<std::uint32_t>(kIndexSize));
  }

  Index index;
  index.segment_offset = ReadLe64(raw + 0x10);
  index.segment_size = ReadLe32(raw + 0x18);
  return index;
}

}  // namespace qsv
