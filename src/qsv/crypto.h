#ifndef QSV2FLV_QSV_CRYPTO_H_
#define QSV2FLV_QSV_CRYPTO_H_

#include <cstdint>

/// \file
/// The two obfuscation schemes a `.qsv` container uses. Neither is
/// cryptography; both are documented in docs/QsvFormat.md.

namespace qsv {

/// XOR against a repeating four-byte pad.
///
/// Self-inverse, so this is also the encryptor — which is what lets
/// tests/support/qsv_builder.h build a version 1 fixture from chosen
/// plaintext.
void DecryptV1(std::uint8_t* buffer, std::uint32_t size);

/// A shuffle driven by a 32-bit state folded out of the buffer.
///
/// Not invertible in practice: the shuffle positions derive from the
/// *ciphertext*, so there is no way to build a version 2 fixture from chosen
/// plaintext. docs/Testing.md covers what is pinned instead.
///
/// `size == 0` returns immediately. The first loop starts at `size - 1` on an
/// unsigned counter, so zero would wrap to 0xFFFFFFFF and run off the buffer.
void DecryptV2(std::uint8_t* buffer, std::uint32_t size);

}  // namespace qsv

#endif  // QSV2FLV_QSV_CRYPTO_H_
