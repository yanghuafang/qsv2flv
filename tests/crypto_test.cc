#include "qsv/crypto.h"

#include <cstdint>
#include <vector>

#include "support/assertions.h"
#include "support/qsv_builder.h"

/// \file
/// Pins the two obfuscation schemes byte for byte.
///
/// These are the only functions in the project whose correctness cannot be
/// argued from first principles at the call site — get one bit wrong and the
/// failure surfaces four layers up as "the demuxer does not recognise this
/// stream". Pinning them here means a change to either is a deliberate change
/// to this file, not a silent one.

namespace {

void CheckV1MatchesTheDerivedPad() {
  test::BeginCase("DecryptV1 pad orientation");

  // DecryptV1 is `buffer[i] ^= pad[~i & 3]`, so eight zero bytes come out as
  // the pad read backwards, twice. Worked out by hand rather than captured from
  // a run: this is the one vector here that is checking the implementation
  // against the format rather than against its own past self. It is also the
  // arm that catches the easiest mistake in the function — indexing the pad
  // forwards, which decodes every real file to noise.
  std::vector<std::uint8_t> buffer(8, 0);
  qsv::DecryptV1(buffer.data(), 8);

  const std::vector<std::uint8_t> expected = {0x79, 0x70, 0x67, 0x62,
                                              0x79, 0x70, 0x67, 0x62};
  CHECK(buffer == expected);
}

void CheckV1IsItsOwnInverse() {
  test::BeginCase("DecryptV1 round trip");

  // The property tests/support/QsvBuilder depends on to build a fixture at all.
  const std::vector<std::uint8_t> original = test::PseudoRandomBytes(1024, 42);
  std::vector<std::uint8_t> buffer = original;

  qsv::DecryptV1(buffer.data(), 1024);
  CHECK(buffer != original);
  qsv::DecryptV1(buffer.data(), 1024);
  CHECK(buffer == original);
}

void CheckV2GoldenVectors() {
  test::BeginCase("DecryptV2 golden vectors");

  // Regression pins, and only that: they were produced by running this
  // implementation once, so they cannot show it matches the client. What they
  // do show is that it still matches itself — which is what a refactor of the
  // rotate-and-swap loop needs, and which nothing else in the suite can
  // provide, because there is no way to build a version 2 fixture to round-trip
  // through (see qsv::DecryptV2).
  //
  // Both lengths are the ones the reader actually calls with: 0x1C for an index
  // record and, below, a stand-in for the 0x400 segment prefix.
  std::vector<std::uint8_t> sixteen = test::PseudoRandomBytes(16, 1);
  qsv::DecryptV2(sixteen.data(), 16);
  const std::vector<std::uint8_t> expected_sixteen = {
      0x99, 0x46, 0xFB, 0x00, 0xE4, 0x40, 0x67, 0x2A,
      0x3E, 0x4C, 0xED, 0x8C, 0x0B, 0x9C, 0x80, 0xB6};
  CHECK(sixteen == expected_sixteen);

  std::vector<std::uint8_t> record = test::PseudoRandomBytes(28, 7);
  qsv::DecryptV2(record.data(), 28);
  const std::vector<std::uint8_t> expected_record = {
      0x01, 0x28, 0xE3, 0x31, 0x6E, 0xA7, 0x75, 0x8E, 0x8D, 0x88,
      0xAA, 0xBE, 0x51, 0x00, 0xB4, 0xCB, 0x2B, 0xAE, 0x04, 0x30,
      0x2A, 0xC3, 0x15, 0xFB, 0xC3, 0xF0, 0xB2, 0xDB};
  CHECK(record == expected_record);
}

void CheckV2DegenerateLengths() {
  test::BeginCase("DecryptV2 degenerate lengths");

  // Length 0 is the guard added to the ported version. The original computed
  // `size - 1` on an unsigned counter and walked 4 GiB backwards off the front
  // of the buffer; under ASan this case is the difference between a clean run
  // and a heap-buffer-underflow. The canary either side is what proves the call
  // wrote nothing at all rather than merely surviving.
  std::vector<std::uint8_t> guarded = {0xAA, 0xBB, 0xCC};
  qsv::DecryptV2(guarded.data() + 1, 0);
  CHECK_EQ(guarded[0], std::uint8_t{0xAA});
  CHECK_EQ(guarded[1], std::uint8_t{0xBB});
  CHECK_EQ(guarded[2], std::uint8_t{0xCC});

  // Length 1 has no byte to shuffle: both loops are empty by construction.
  std::vector<std::uint8_t> single = {0x5A};
  qsv::DecryptV2(single.data(), 1);
  CHECK_EQ(single[0], std::uint8_t{0x5A});
}

}  // namespace

int main() {
  CheckV1MatchesTheDerivedPad();
  CheckV1IsItsOwnInverse();
  CheckV2GoldenVectors();
  CheckV2DegenerateLengths();
  return test::Finish("CryptoTest");
}
