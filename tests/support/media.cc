#include "support/media.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cstring>
#include <limits>

namespace test {
namespace {

constexpr int kIoBufferSize = 0x8000;
constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr int kFrameRate = 25;

/// FFmpeg 7.0 (libavformat 61) made the AVIO write callback take a
/// `const uint8_t*`. Passing the wrong one is a hard compile error rather
/// than a warning, so this is the one place the project has to know which
/// FFmpeg it was built against. Both signatures are still in the field: a
/// distribution carries one FFmpeg for years, while Homebrew tracks the
/// newest, so the two ends of the supported range straddle this change.
#if LIBAVFORMAT_VERSION_MAJOR >= 61
using WriteBuffer = const std::uint8_t*;
#else
using WriteBuffer = std::uint8_t*;
#endif

int WriteToVector(void* opaque, WriteBuffer buffer, int size) {
  auto* sink = static_cast<std::vector<std::uint8_t>*>(opaque);
  sink->insert(sink->end(), buffer, buffer + size);
  return size;
}

struct ReadCursor {
  const std::vector<std::uint8_t>* bytes = nullptr;
  std::size_t position = 0;
};

int ReadFromVector(void* opaque, std::uint8_t* buffer, int size) {
  auto* cursor = static_cast<ReadCursor*>(opaque);
  const std::size_t left = cursor->bytes->size() - cursor->position;
  if (left == 0) {
    return AVERROR_EOF;
  }
  const std::size_t count = std::min(left, static_cast<std::size_t>(size));
  std::memcpy(buffer, cursor->bytes->data() + cursor->position, count);
  cursor->position += count;
  return static_cast<int>(count);
}

std::int64_t SeekVector(void* opaque, std::int64_t offset, int whence) {
  auto* cursor = static_cast<ReadCursor*>(opaque);
  const auto size = static_cast<std::int64_t>(cursor->bytes->size());
  // AVSEEK_SIZE is a query, not a move: answering it lets the MP4 demuxer find
  // the trailing index without walking the file.
  if (whence == AVSEEK_SIZE) {
    return size;
  }
  std::int64_t target = offset;
  if (whence == SEEK_CUR) {
    target += static_cast<std::int64_t>(cursor->position);
  } else if (whence == SEEK_END) {
    target += size;
  }
  if (target < 0 || target > size) {
    return AVERROR(EINVAL);
  }
  cursor->position = static_cast<std::size_t>(target);
  return target;
}

/// Fill one frame with a moving gradient. Any deterministic picture would do;
/// a gradient just compresses to something more like real video than flat grey.
void Paint(AVFrame* frame, int index, std::uint32_t seed) {
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      frame->data[0][y * frame->linesize[0] + x] =
          static_cast<std::uint8_t>(x + y + index * 3 + seed);
    }
  }
  for (int y = 0; y < kHeight / 2; ++y) {
    for (int x = 0; x < kWidth / 2; ++x) {
      frame->data[1][y * frame->linesize[1] + x] =
          static_cast<std::uint8_t>(128 + x + index);
      frame->data[2][y * frame->linesize[2] + x] =
          static_cast<std::uint8_t>(128 + y - index);
    }
  }
}

/// Drain the encoder into the muxer. `frame` is null to flush.
bool Drain(AVFormatContext* format, AVCodecContext* codec, AVStream* stream,
           AVPacket* packet, AVFrame* frame) {
  if (avcodec_send_frame(codec, frame) < 0) {
    return false;
  }
  while (true) {
    const int status = avcodec_receive_packet(codec, packet);
    if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) {
      return true;
    }
    if (status < 0) {
      return false;
    }
    av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
    packet->stream_index = stream->index;
    if (av_interleaved_write_frame(format, packet) < 0) {
      return false;
    }
  }
}

DemuxSummary Summarize(AVFormatContext* format) {
  DemuxSummary summary;
  if (avformat_find_stream_info(format, nullptr) < 0) {
    return summary;
  }

  summary.stream_count = static_cast<int>(format->nb_streams);
  for (unsigned int i = 0; i < format->nb_streams; ++i) {
    if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      summary.video_codec_id = format->streams[i]->codecpar->codec_id;
      break;
    }
  }

  std::vector<std::int64_t> last_dts(format->nb_streams,
                                     std::numeric_limits<std::int64_t>::min());
  AVPacket* packet = av_packet_alloc();
  if (packet == nullptr) {
    return summary;
  }

  while (av_read_frame(format, packet) >= 0) {
    ++summary.packet_count;
    const int index = packet->stream_index;
    const AVRational base = format->streams[index]->time_base;

    if (packet->dts != AV_NOPTS_VALUE) {
      if (last_dts[index] != std::numeric_limits<std::int64_t>::min() &&
          packet->dts <= last_dts[index]) {
        summary.dts_monotonic = false;
      }
      last_dts[index] = packet->dts;
      summary.end_us = std::max(
          summary.end_us,
          av_rescale_q(packet->dts + packet->duration, base, AV_TIME_BASE_Q));
    }
    av_packet_unref(packet);
  }

  av_packet_free(&packet);
  summary.ok = true;
  return summary;
}

}  // namespace

Clip MakeMpegTsClip(int frames, std::uint32_t seed) {
  Clip clip;

  const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
  if (encoder == nullptr) {
    return clip;
  }

  AVFormatContext* format = nullptr;
  avformat_alloc_output_context2(&format, nullptr, "mpegts", nullptr);
  if (format == nullptr) {
    return clip;
  }

  std::vector<std::uint8_t> sink;
  auto* io_buffer = static_cast<std::uint8_t*>(av_malloc(kIoBufferSize));
  AVIOContext* io =
      avio_alloc_context(io_buffer, kIoBufferSize, /*write_flag=*/1, &sink,
                         nullptr, WriteToVector, nullptr);
  format->pb = io;
  format->flags |= AVFMT_FLAG_CUSTOM_IO;

  AVCodecContext* codec = avcodec_alloc_context3(encoder);
  codec->width = kWidth;
  codec->height = kHeight;
  codec->pix_fmt = AV_PIX_FMT_YUV420P;
  codec->time_base = AVRational{1, kFrameRate};
  codec->framerate = AVRational{kFrameRate, 1};
  codec->bit_rate = 400000;
  codec->gop_size = 12;
  // No B-frames, so one input frame is one output packet and the packet count
  // a test asserts on is the frame count it asked for.
  codec->max_b_frames = 0;
  if ((format->oformat->flags & AVFMT_GLOBALHEADER) != 0) {
    codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
  }

  AVStream* stream = avformat_new_stream(format, nullptr);
  AVFrame* frame = av_frame_alloc();
  AVPacket* packet = av_packet_alloc();
  bool ok = stream != nullptr && frame != nullptr && packet != nullptr &&
            io != nullptr && avcodec_open2(codec, encoder, nullptr) == 0;

  if (ok) {
    avcodec_parameters_from_context(stream->codecpar, codec);
    stream->time_base = codec->time_base;
    frame->format = codec->pix_fmt;
    frame->width = codec->width;
    frame->height = codec->height;
    ok = av_frame_get_buffer(frame, 0) == 0 &&
         avformat_write_header(format, nullptr) == 0;
  }

  if (ok) {
    for (int i = 0; i < frames && ok; ++i) {
      ok = av_frame_make_writable(frame) == 0;
      if (!ok) {
        break;
      }
      Paint(frame, i, seed);
      frame->pts = i;
      ok = Drain(format, codec, stream, packet, frame);
    }
    ok = ok && Drain(format, codec, stream, packet, nullptr);
    ok = ok && av_write_trailer(format) == 0;
  }

  av_packet_free(&packet);
  av_frame_free(&frame);
  avcodec_free_context(&codec);
  if (io != nullptr) {
    av_freep(&io->buffer);
    avio_context_free(&io);
  }
  format->pb = nullptr;
  avformat_free_context(format);

  if (ok) {
    clip.bytes = std::move(sink);
    clip.frames = frames;
  }
  return clip;
}

DemuxSummary SummarizeFile(const std::string& path) {
  AVFormatContext* format = nullptr;
  if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) {
    return {};
  }
  const DemuxSummary summary = Summarize(format);
  avformat_close_input(&format);
  return summary;
}

DemuxSummary SummarizeBuffer(const std::vector<std::uint8_t>& bytes) {
  ReadCursor cursor{&bytes, 0};
  auto* io_buffer = static_cast<std::uint8_t*>(av_malloc(kIoBufferSize));
  AVIOContext* io =
      avio_alloc_context(io_buffer, kIoBufferSize, /*write_flag=*/0, &cursor,
                         ReadFromVector, nullptr, SeekVector);
  if (io == nullptr) {
    av_free(io_buffer);
    return {};
  }

  AVFormatContext* format = avformat_alloc_context();
  format->pb = io;
  format->flags |= AVFMT_FLAG_CUSTOM_IO;

  DemuxSummary summary;
  if (avformat_open_input(&format, "", nullptr, nullptr) >= 0) {
    summary = Summarize(format);
    avformat_close_input(&format);
  }
  av_freep(&io->buffer);
  avio_context_free(&io);
  return summary;
}

void SilenceFfmpegLog() { av_log_set_level(AV_LOG_ERROR); }

QuietFfmpegLog::QuietFfmpegLog() : previous_(av_log_get_level()) {
  av_log_set_level(AV_LOG_QUIET);
}

QuietFfmpegLog::~QuietFfmpegLog() { av_log_set_level(previous_); }

}  // namespace test
