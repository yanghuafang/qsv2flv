#include "qsv/crypto.h"

namespace qsv {

void DecryptV1(std::uint8_t* buffer, std::uint32_t size) {
  static const std::uint8_t kPad[] = {0x62, 0x67, 0x70, 0x79};
  for (std::uint32_t i = 0; i < size; ++i) {
    // ~i & 3 walks the pad backwards (3, 2, 1, 0, 3, ...), which is what the
    // client does; a forward i & 3 decodes to garbage.
    buffer[i] ^= kPad[~i & 0x3];
  }
}

void DecryptV2(std::uint8_t* buffer, std::uint32_t size) {
  if (size == 0) {
    return;
  }

  // 0x62677079 is "bgpy" — the same four bytes as DecryptV1's pad, read as a
  // big-endian word.
  std::uint32_t x = 0x62677079;

  // First pass: fold the whole buffer into x, high index to low. Note that it
  // reads the buffer without writing it, so the state below is a function of
  // the ciphertext alone.
  for (std::uint32_t i = size - 1; i != 0; --i) {
    x = (x << 1) | (x >> 31);
    x ^= buffer[i];
  }

  // Second pass: advance x one byte at a time and swap position i with the
  // position it selects. buffer[i] is still ciphertext when it is read here —
  // step i only ever writes indices i and j, and j < i — so the sequence of
  // swap positions is fixed by the input before any of this runs.
  for (std::uint32_t i = 1; i < size; ++i) {
    x ^= buffer[i] & 0xFF;
    x = (x >> 1) | (x << 31);
    const std::uint32_t j = x % i;
    const std::uint8_t tmp = buffer[j];
    buffer[j] = tmp ^ static_cast<std::uint8_t>(~buffer[i]);
    buffer[i] = tmp;
  }
}

}  // namespace qsv
