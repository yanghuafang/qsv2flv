#include "qsv/reader.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "qsv/types.h"
#include "support/assertions.h"
#include "support/qsv_builder.h"
#include "support/temp_dir.h"

/// \file
/// qsv::Reader against synthetic containers: one happy path, one fixture per
/// way of being broken, and a pass of pure garbage.
///
/// Every fixture here is version 1, because version 1 is the only one that can
/// be built — docs/Testing.md sets out why, and what covers version 2 instead.

namespace {

/// Read a whole segment through Reader::Read(), in `chunk`-byte requests.
///
/// Chunk size is a parameter because it is what breaks: the first read is
/// served from the prefix and stops at its boundary, so a caller assuming a
/// full buffer works at 32 KiB and fails at 100 bytes.
std::vector<std::uint8_t> ReadSegment(qsv::Reader& reader, int chunk) {
  std::vector<std::uint8_t> out;
  std::vector<std::uint8_t> buffer(static_cast<std::size_t>(chunk));
  while (true) {
    const int count = reader.Read(buffer.data(), chunk);
    if (count <= 0) {
      break;
    }
    out.insert(out.end(), buffer.begin(), buffer.begin() + count);
  }
  return out;
}

test::QsvBuilder ThreeSegmentBuilder() {
  test::QsvBuilder builder(1);
  builder.AddSegment(test::PseudoRandomBytes(0x400, 11));      // prefix only
  builder.AddSegment(test::PseudoRandomBytes(0x400 + 1, 12));  // one byte over
  builder.AddSegment(test::PseudoRandomBytes(0x2000, 13));     // several reads
  return builder;
}

void CheckHappyPath(const test::TempDir& temp) {
  test::BeginCase("valid version 1 container");

  const test::QsvBuilder builder = ThreeSegmentBuilder();
  const std::string path = temp.WriteFile("good.qsv", builder.Build());

  qsv::Reader reader(path);
  CHECK_EQ(reader.status(), qsv::Status::kOk);
  CHECK_EQ(reader.version(), std::uint32_t{1});
  CHECK_EQ(reader.segment_count(), 3);

  std::int64_t expected_total = 0;
  for (std::size_t i = 0; i < builder.segment_count(); ++i) {
    expected_total += static_cast<std::int64_t>(builder.Payload(i).size());
  }
  CHECK_EQ(reader.total_size(), expected_total);
  CHECK_EQ(reader.processed_size(), std::int64_t{0});

  // Three chunk sizes: under the prefix, exactly the prefix, over the segment.
  const int chunks[] = {100, 0x400, 0x8000};
  for (int chunk : chunks) {
    for (int segment = 0; segment < 3; ++segment) {
      CHECK(reader.SeekToSegment(segment));
      CHECK(ReadSegment(reader, chunk) ==
            builder.Payload(static_cast<std::size_t>(segment)));
    }
  }

  // Past the end, Read() reports end rather than error — the distinction
  // remux/ uses to tell a finished segment from a truncated file.
  CHECK(reader.SeekToSegment(0));
  std::vector<std::uint8_t> sink(0x400);
  CHECK_EQ(reader.Read(sink.data(), 0x400), 0x400);
  CHECK_EQ(reader.Read(sink.data(), 0x400), 0);
  CHECK_EQ(reader.status(), qsv::Status::kOk);
}

void CheckProgressAccounting(const test::TempDir& temp) {
  test::BeginCase("progress accounting");

  const test::QsvBuilder builder = ThreeSegmentBuilder();
  const std::string path = temp.WriteFile("progress.qsv", builder.Build());

  qsv::Reader reader(path);
  CHECK_EQ(reader.progress(), 0.0);

  CHECK(reader.SeekToSegment(0));
  CHECK(ReadSegment(reader, 0x8000).size() == builder.Payload(0).size());
  CHECK_EQ(reader.processed_size(),
           static_cast<std::int64_t>(builder.Payload(0).size()));

  CHECK(reader.SeekToSegment(1));
  CHECK(ReadSegment(reader, 0x8000).size() == builder.Payload(1).size());
  CHECK(reader.SeekToSegment(2));
  CHECK(ReadSegment(reader, 0x8000).size() == builder.Payload(2).size());
  CHECK_EQ(reader.processed_size(), reader.total_size());
  CHECK_EQ(reader.progress(), 1.0);

  // Derived, not accumulated: seeking backwards reports where the reader now
  // is, not the high-water mark it reached earlier.
  CHECK(reader.SeekToSegment(0));
  CHECK_EQ(reader.processed_size(), std::int64_t{0});
}

void CheckProgressArithmeticAboveTwoGigabytes() {
  test::BeginCase("progress arithmetic past 2 GiB");

  // 32-bit counters overflow to negative on a feature-length file. No fixture
  // can reach this without a real 2 GiB file, hence the free function.
  constexpr std::int64_t kFourGiB = 4LL * 1024 * 1024 * 1024;
  CHECK_EQ(qsv::ProgressOf(kFourGiB / 2, kFourGiB), 0.5);
  CHECK_EQ(qsv::ProgressOf(kFourGiB, kFourGiB), 1.0);
  CHECK(qsv::ProgressOf(1, kFourGiB * 8) > 0.0);

  // Degenerate inputs answer rather than divide by zero or run past 1.
  CHECK_EQ(qsv::ProgressOf(0, 0), 0.0);
  CHECK_EQ(qsv::ProgressOf(10, -1), 0.0);
  CHECK_EQ(qsv::ProgressOf(20, 10), 1.0);
}

void CheckRejections(const test::TempDir& temp) {
  auto status_of = [&](const std::string& name,
                       const std::vector<std::uint8_t>& bytes) {
    return qsv::Reader(temp.WriteFile(name, bytes)).status();
  };

  test::BeginCase("missing file");
  CHECK_EQ(qsv::Reader(temp.FilePath("absent.qsv")).status(),
           qsv::Status::kCannotOpen);

  test::BeginCase("bad signature");
  {
    test::QsvBuilder builder = ThreeSegmentBuilder();
    builder.SetSignature("NOT A QSV!");
    CHECK_EQ(status_of("signature.qsv", builder.Build()),
             qsv::Status::kBadSignature);
  }

  test::BeginCase("unsupported version");
  {
    test::QsvBuilder builder = ThreeSegmentBuilder();
    builder.SetDeclaredVersion(3);
    CHECK_EQ(status_of("version.qsv", builder.Build()),
             qsv::Status::kUnsupportedVersion);
  }

  test::BeginCase("index count out of range");
  {
    test::QsvBuilder zero = ThreeSegmentBuilder();
    zero.SetDeclaredIndexCount(0);
    CHECK_EQ(status_of("zero-index.qsv", zero.Build()), qsv::Status::kCorrupt);

    test::QsvBuilder huge = ThreeSegmentBuilder();
    huge.SetDeclaredIndexCount(qsv::kMaxIndexCount + 1);
    CHECK_EQ(status_of("huge-index.qsv", huge.Build()), qsv::Status::kCorrupt);

    // In range but larger than the file: caught by the length check, not by
    // allocating 65535 records and reading past the table.
    test::QsvBuilder overshoot = ThreeSegmentBuilder();
    overshoot.SetDeclaredIndexCount(qsv::kMaxIndexCount);
    CHECK_EQ(status_of("overshoot-index.qsv", overshoot.Build()),
             qsv::Status::kTruncated);
  }

  test::BeginCase("truncated file");
  {
    test::QsvBuilder short_header = ThreeSegmentBuilder();
    short_header.TruncateTo(qsv::kHeaderSize - 1);
    CHECK_EQ(status_of("short-header.qsv", short_header.Build()),
             qsv::Status::kTruncated);

    test::QsvBuilder short_table = ThreeSegmentBuilder();
    short_table.TruncateTo(qsv::kHeaderSize + 1);
    CHECK_EQ(status_of("short-table.qsv", short_table.Build()),
             qsv::Status::kTruncated);
  }

  test::BeginCase("index record does not describe the file");
  {
    test::QsvBuilder past_end = ThreeSegmentBuilder();
    past_end.SetSegmentOffset(1, 1u << 30);
    CHECK_EQ(status_of("past-end.qsv", past_end.Build()),
             qsv::Status::kCorrupt);

    // `offset + size > file_size` wraps this close to the top of the range:
    // the sum comes out small, the record passes, the seek is unbounded.
    test::QsvBuilder wrapping = ThreeSegmentBuilder();
    wrapping.SetSegmentOffset(0, std::numeric_limits<std::uint64_t>::max() - 8);
    CHECK_EQ(status_of("wrapping.qsv", wrapping.Build()),
             qsv::Status::kCorrupt);

    // Shorter than the encrypted prefix, which every segment has by definition.
    test::QsvBuilder tiny = ThreeSegmentBuilder();
    tiny.SetSegmentSize(2, qsv::kEncryptedPrefixSize - 1);
    CHECK_EQ(status_of("tiny-segment.qsv", tiny.Build()),
             qsv::Status::kCorrupt);
  }

  test::BeginCase("reader stays failed");
  {
    test::QsvBuilder builder = ThreeSegmentBuilder();
    builder.SetSignature("NOT A QSV!");
    qsv::Reader reader(temp.WriteFile("stays-failed.qsv", builder.Build()));
    CHECK(!reader.ok());
    CHECK(!reader.SeekToSegment(0));
    std::uint8_t byte = 0;
    CHECK_EQ(reader.Read(&byte, 1), -1);
  }

  test::BeginCase("read before seek");
  {
    qsv::Reader reader(
        temp.WriteFile("no-seek.qsv", ThreeSegmentBuilder().Build()));
    CHECK(reader.ok());
    std::uint8_t byte = 0;
    CHECK_EQ(reader.Read(&byte, 1), -1);
    CHECK(!reader.SeekToSegment(-1));
    CHECK(!reader.SeekToSegment(3));
  }
}

void CheckVersionTwoIsRejectedNotTrusted(const test::TempDir& temp) {
  test::BeginCase("version 2 container this builder cannot encode");

  // The builder writes index records in the clear, so a declared version 2
  // makes the reader decrypt plaintext into noise: effectively random offsets
  // and sizes, every one of which must be rejected rather than seeked to.
  // Under ASan this is the arm that catches a read sized from an unvalidated
  // field.
  test::QsvBuilder builder(2);
  builder.AddSegment(test::PseudoRandomBytes(0x800, 21));
  builder.AddSegment(test::PseudoRandomBytes(0x800, 22));

  qsv::Reader reader(temp.WriteFile("version2.qsv", builder.Build()));
  CHECK_EQ(reader.status(), qsv::Status::kCorrupt);
}

void CheckGarbageIsSurvivable(const test::TempDir& temp) {
  test::BeginCase("garbage never crashes");

  // 200 buffers carrying a valid signature and nothing else the parser can
  // trust. Not a fuzzer, but the same idea at unit-test size.
  // Every path through the header, the version check, the index count, and the
  // record validation gets driven with values no hand-written fixture would
  // pick. The assertion is deliberately weak — the point is what runs
  // underneath it, since this is the arm the sanitizer job is really exercising
  // (docs/Testing.md).
  for (std::uint32_t seed = 1; seed <= 200; ++seed) {
    std::vector<std::uint8_t> bytes = test::PseudoRandomBytes(4096, seed);
    for (std::size_t i = 0; i < qsv::kSignatureSize; ++i) {
      bytes[i] = static_cast<std::uint8_t>(qsv::kSignature[i]);
    }
    // Half the cases declare a version the reader accepts, so the run gets past
    // the version check and into the index table rather than stopping early.
    bytes[0x0A] = static_cast<std::uint8_t>((seed % 2) + 1);
    bytes[0x0B] = 0;
    bytes[0x0C] = 0;
    bytes[0x0D] = 0;

    qsv::Reader reader(temp.WriteFile("garbage.qsv", bytes));
    if (reader.ok()) {
      // A container that validated by chance still has to be readable without
      // running off anything.
      for (int segment = 0; segment < reader.segment_count(); ++segment) {
        if (reader.SeekToSegment(segment)) {
          ReadSegment(reader, 997);
        }
      }
    }
  }
  CHECK(true);
}

}  // namespace

int main() {
  const test::TempDir temp("reader");
  CheckHappyPath(temp);
  CheckProgressAccounting(temp);
  CheckProgressArithmeticAboveTwoGigabytes();
  CheckRejections(temp);
  CheckVersionTwoIsRejectedNotTrusted(temp);
  CheckGarbageIsSurvivable(temp);
  return test::Finish("ReaderTest");
}
