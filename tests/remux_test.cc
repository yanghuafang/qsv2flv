extern "C" {
#include <libavcodec/codec_id.h>
}

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "qsv/reader.h"
#include "remux/remuxer.h"
#include "support/assertions.h"
#include "support/media.h"
#include "support/qsv_builder.h"
#include "support/temp_dir.h"

/// \file
/// The end-to-end test: a `.qsv` built here, converted by the real code path,
/// and then demuxed to see whether what came out is the video that went in.
///
/// No sample file: tests/support/media.cc encodes MPEG-TS in memory and
/// tests/support/qsv_builder.h wraps two clips as a version 1 container. From
/// there it is the same qsv::Reader and remux::Remux() the front ends call.
///
/// The packet count is measured from the clips rather than written here, so
/// the assertion is exact without pinning it to one FFmpeg's packetisation.

namespace {

constexpr int kFramesPerClip = 20;

struct Fixture {
  std::string qsv_path;
  int expected_packets = 0;
  std::int64_t expected_end_us = 0;
};

/// Two clips, wrapped as two segments. Returns false when this FFmpeg build
/// cannot encode MPEG-2, which is a skip rather than a failure.
bool BuildFixture(const test::TempDir& temp, const std::string& name,
                  int segments, Fixture& fixture) {
  test::QsvBuilder builder(1);
  for (int i = 0; i < segments; ++i) {
    const test::Clip clip =
        test::MakeMpegTsClip(kFramesPerClip, static_cast<std::uint32_t>(i * 7));
    if (!clip.valid()) {
      return false;
    }

    const test::DemuxSummary alone = test::SummarizeBuffer(clip.bytes);
    if (!alone.ok) {
      return false;
    }
    fixture.expected_packets += alone.packet_count;
    fixture.expected_end_us += alone.end_us;

    builder.AddSegment(clip.bytes);
  }

  fixture.qsv_path = temp.WriteFile(name, builder.Build());
  return true;
}

void CheckTwoSegmentsJoin(const test::TempDir& temp) {
  test::BeginCase("two segments remux into one MP4");

  Fixture fixture;
  if (!BuildFixture(temp, "two.qsv", 2, fixture)) {
    std::cout << "  skipped: this FFmpeg has no MPEG-2 encoder\n";
    return;
  }

  qsv::Reader reader(fixture.qsv_path);
  CHECK_EQ(reader.status(), qsv::Status::kOk);
  CHECK_EQ(reader.segment_count(), 2);

  const std::string output = temp.FilePath("two.mp4");
  const remux::Result result = remux::Remux(reader, output);
  CHECK(result.ok);
  CHECK_EQ(result.message, std::string());
  CHECK_EQ(result.dropped_packets, 0);
  CHECK(test::FileSize(output) > 0);

  const test::DemuxSummary summary = test::SummarizeFile(output);
  CHECK(summary.ok);
  CHECK_EQ(summary.stream_count, 1);
  CHECK_EQ(summary.video_codec_id, static_cast<int>(AV_CODEC_ID_MPEG2VIDEO));

  // Nothing dropped, nothing invented.
  CHECK_EQ(summary.packet_count, fixture.expected_packets);

  // What the timestamp handling exists for: both segments start at zero, so a
  // naive copy makes the second one's DTS go backwards.
  CHECK(summary.dts_monotonic);

  // Appended rather than overlaid. A wide band: the duration of 20 MPEG-2
  // frames depends on the muxer's rounding.
  CHECK(summary.end_us > fixture.expected_end_us * 3 / 4);
  CHECK(summary.end_us < fixture.expected_end_us * 5 / 4);
}

void CheckSingleSegment(const test::TempDir& temp) {
  test::BeginCase("one segment");

  Fixture fixture;
  if (!BuildFixture(temp, "one.qsv", 1, fixture)) {
    return;
  }

  qsv::Reader reader(fixture.qsv_path);
  const std::string output = temp.FilePath("one.mp4");
  const remux::Result result = remux::Remux(reader, output);
  CHECK(result.ok);

  const test::DemuxSummary summary = test::SummarizeFile(output);
  CHECK(summary.ok);
  CHECK_EQ(summary.packet_count, fixture.expected_packets);
  CHECK(summary.dts_monotonic);
}

void CheckProgressReachesOne(const test::TempDir& temp) {
  test::BeginCase("progress callback");

  Fixture fixture;
  if (!BuildFixture(temp, "progress.qsv", 2, fixture)) {
    return;
  }

  qsv::Reader reader(fixture.qsv_path);
  double highest = -1.0;
  bool monotonic = true;
  const remux::Result result =
      remux::Remux(reader, temp.FilePath("progress.mp4"), [&](double value) {
        monotonic = monotonic && value >= highest;
        highest = value;
        return true;
      });

  CHECK(result.ok);
  CHECK(monotonic);
  CHECK_EQ(highest, 1.0);
}

void CheckCancellationRemovesTheOutput(const test::TempDir& temp) {
  test::BeginCase("cancellation");

  Fixture fixture;
  if (!BuildFixture(temp, "cancel.qsv", 2, fixture)) {
    return;
  }

  qsv::Reader reader(fixture.qsv_path);
  const std::string output = temp.FilePath("cancel.mp4");
  const remux::Result result =
      remux::Remux(reader, output, [](double) { return false; });

  CHECK(!result.ok);
  CHECK(result.cancelled);
  CHECK(result.message.empty());

  // A half-written MP4 has no trailer and no index, so it plays as a corrupt
  // file rather than a short one. Leaving it behind for the user to find is
  // worse than leaving nothing.
  CHECK_EQ(test::FileSize(output), std::int64_t{-1});
}

void CheckFailurePaths(const test::TempDir& temp) {
  test::BeginCase("unopenable input");
  {
    qsv::Reader reader(temp.FilePath("absent.qsv"));
    const remux::Result result =
        remux::Remux(reader, temp.FilePath("absent.mp4"));
    CHECK(!result.ok);
    CHECK(!result.cancelled);
    CHECK_EQ(result.message,
             std::string(qsv::Describe(qsv::Status::kCannotOpen)));
    CHECK_EQ(test::FileSize(temp.FilePath("absent.mp4")), std::int64_t{-1});
  }

  test::BeginCase("unknown output container");
  {
    Fixture fixture;
    if (!BuildFixture(temp, "container.qsv", 1, fixture)) {
      return;
    }
    qsv::Reader reader(fixture.qsv_path);
    // No muxer answers to this extension, so the failure has to come from
    // avformat_alloc_output_context2 rather than from something further in.
    // FFmpeg says so on stderr, which is correct of it and misleading in a
    // passing run, hence the guard.
    const test::QuietFfmpegLog quiet;
    const remux::Result result =
        remux::Remux(reader, temp.FilePath("output.not-a-container"));
    CHECK(!result.ok);
    CHECK(result.message.rfind("avformat_alloc_output_context2", 0) == 0);
  }
}

}  // namespace

int main() {
  test::SilenceFfmpegLog();
  const test::TempDir temp("remux");

  CheckTwoSegmentsJoin(temp);
  CheckSingleSegment(temp);
  CheckProgressReachesOne(temp);
  CheckCancellationRemovesTheOutput(temp);
  CheckFailurePaths(temp);

  return test::Finish("RemuxTest");
}
