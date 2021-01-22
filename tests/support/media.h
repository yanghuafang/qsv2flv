#ifndef QSV2FLV_TESTS_SUPPORT_MEDIA_H_
#define QSV2FLV_TESTS_SUPPORT_MEDIA_H_

#include <cstdint>
#include <string>
#include <vector>

/// \file
/// Real media, made in memory, so the end-to-end test needs no sample file.
///
/// MPEG-TS because that is what a segment holds; MPEG-2 because its encoder is
/// native to libavcodec, where H.264 would depend on the local build having
/// x264. A segment is a complete stream, so one clip stands in for one
/// segment.

namespace test {

/// An encoded clip, plus what went into it.
struct Clip {
  std::vector<std::uint8_t> bytes;
  int frames = 0;

  /// False when this FFmpeg build has no MPEG-2 encoder; callers skip rather
  /// than fail.
  [[nodiscard]] bool valid() const { return !bytes.empty(); }
};

/// Encode `frames` frames of 64x64 MPEG-2 into an MPEG-TS stream. `seed`
/// shifts the picture, so a test cannot pass by reading one segment twice.
Clip MakeMpegTsClip(int frames, std::uint32_t seed);

/// What a demuxer sees in a finished file or buffer.
struct DemuxSummary {
  bool ok = false;
  int stream_count = 0;
  int video_codec_id = 0;
  int packet_count = 0;

  /// False if any stream's DTS went backwards or repeated. Two clips each
  /// starting at zero, concatenated naively, fail this.
  bool dts_monotonic = true;

  /// Largest end timestamp seen, in microseconds.
  std::int64_t end_us = 0;
};

DemuxSummary SummarizeFile(const std::string& path);
DemuxSummary SummarizeBuffer(const std::vector<std::uint8_t>& bytes);

/// Quiet libavformat for the whole executable; without it every fixture prints
/// a stream dump.
void SilenceFfmpegLog();

/// Silences FFmpeg completely while alive, for cases that provoke a failure on
/// purpose — an expected error printed by a passing suite is indistinguishable
/// from a real one.
class QuietFfmpegLog {
 public:
  QuietFfmpegLog();
  ~QuietFfmpegLog();
  QuietFfmpegLog(const QuietFfmpegLog&) = delete;
  QuietFfmpegLog& operator=(const QuietFfmpegLog&) = delete;

 private:
  int previous_;
};

}  // namespace test

#endif  // QSV2FLV_TESTS_SUPPORT_MEDIA_H_
