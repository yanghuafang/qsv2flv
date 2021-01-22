#include "remux/remuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

namespace remux {
namespace {

/// What FFmpeg's own demuxers use, so avformat_find_stream_info() rarely has
/// to seek backwards through it.
constexpr int kIoBufferSize = 0x8000;

/// Minimum progress step. The GUI redraws a row per call, so a callback per
/// packet would cost more than the copy it reports on.
constexpr double kProgressStep = 0.01;

/// AVIO read callback.
///
/// AVERROR_EOF, not 0: since libavformat 59 a 0-byte read means "try again",
/// which stalls the demuxer instead of ending the segment.
int ReadSegment(void* opaque, std::uint8_t* buffer, int size) {
  auto* reader = static_cast<qsv::Reader*>(opaque);
  const int count = reader->Read(buffer, size);
  if (count > 0) {
    return count;
  }
  return count == 0 ? AVERROR_EOF : AVERROR(EIO);
}

/// One segment, opened as an AVFormatContext reading through a qsv::Reader.
///
/// Owns the AVIOContext as well as the format context: avformat_open_input()
/// sets AVFMT_FLAG_CUSTOM_IO when `pb` is already populated, and
/// avformat_close_input() then leaves that `pb` to the caller.
class SegmentInput {
 public:
  SegmentInput() = default;
  ~SegmentInput() { Close(); }
  SegmentInput(const SegmentInput&) = delete;
  SegmentInput& operator=(const SegmentInput&) = delete;

  bool Open(qsv::Reader& reader, std::string& error) {
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(kIoBufferSize));
    if (buffer == nullptr) {
      error = "av_malloc";
      return false;
    }

    io_ = avio_alloc_context(buffer, kIoBufferSize, /*write_flag=*/0, &reader,
                             ReadSegment, nullptr, nullptr);
    if (io_ == nullptr) {
      av_free(buffer);
      error = "avio_alloc_context";
      return false;
    }

    ctx_ = avformat_alloc_context();
    if (ctx_ == nullptr) {
      error = "avformat_alloc_context";
      return false;
    }
    ctx_->pb = io_;
    ctx_->flags |= AVFMT_FLAG_CUSTOM_IO;

    // avformat_open_input() frees and nulls ctx_ on failure, so Close() has
    // nothing left to do for it — but io_ still needs releasing either way.
    if (avformat_open_input(&ctx_, "", nullptr, nullptr) < 0) {
      error = "avformat_open_input";
      return false;
    }
    if (avformat_find_stream_info(ctx_, nullptr) < 0) {
      error = "avformat_find_stream_info";
      return false;
    }
    return true;
  }

  [[nodiscard]] AVFormatContext* get() const { return ctx_; }

  void Close() {
    if (ctx_ != nullptr) {
      avformat_close_input(&ctx_);
    }
    if (io_ != nullptr) {
      // io_->buffer, not the pointer handed to avio_alloc_context: FFmpeg
      // reallocates the buffer when a demuxer asks to grow it, and frees the
      // one it is holding.
      av_freep(&io_->buffer);
      avio_context_free(&io_);
    }
  }

 private:
  AVFormatContext* ctx_ = nullptr;
  AVIOContext* io_ = nullptr;
};

/// The output file: one muxer, built from the first segment's stream layout.
class Output {
 public:
  Output() = default;
  ~Output() { Close(); }
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;

  bool Open(const std::string& path, AVFormatContext* input,
            std::string& error) {
    avformat_alloc_output_context2(&ctx_, nullptr, nullptr, path.c_str());
    if (ctx_ == nullptr) {
      error = "avformat_alloc_output_context2 (unknown container extension)";
      return false;
    }

    for (unsigned int i = 0; i < input->nb_streams; ++i) {
      const AVStream* in = input->streams[i];

      // nullptr, not avcodec_find_decoder(): a remux never decodes, and asking
      // would refuse a codec this build cannot decode but could copy.
      AVStream* out = avformat_new_stream(ctx_, nullptr);
      if (out == nullptr) {
        error = "avformat_new_stream";
        return false;
      }
      if (avcodec_parameters_copy(out->codecpar, in->codecpar) < 0) {
        error = "avcodec_parameters_copy";
        return false;
      }
      // Let the muxer pick the tag for its own container; the input's belongs
      // to the input's.
      out->codecpar->codec_tag = 0;
      // A hint only: avformat_write_header() overwrites it, which is why
      // packets are rescaled after that call and not before.
      out->time_base = in->time_base;
    }

    if ((ctx_->oformat->flags & AVFMT_NOFILE) == 0) {
      if (avio_open(&ctx_->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
        error = "avio_open (cannot create the output file)";
        return false;
      }
      opened_ = true;
    }
    return true;
  }

  bool WriteHeader(std::string& error) {
    AVDictionary* options = nullptr;
    // Index at the front, so the file plays before it finishes downloading.
    // Ignored by muxers without the option.
    av_dict_set(&options, "movflags", "faststart", 0);
    const int status = avformat_write_header(ctx_, &options);
    av_dict_free(&options);

    if (status < 0) {
      error = "avformat_write_header";
      return false;
    }
    header_written_ = true;
    return true;
  }

  bool WriteTrailer(std::string& error) {
    if (av_write_trailer(ctx_) < 0) {
      error = "av_write_trailer";
      return false;
    }
    header_written_ = false;
    return true;
  }

  [[nodiscard]] AVFormatContext* get() const { return ctx_; }
  [[nodiscard]] bool opened() const { return opened_; }

  void Close() {
    if (ctx_ == nullptr) {
      return;
    }
    if (ctx_->pb != nullptr && (ctx_->oformat->flags & AVFMT_NOFILE) == 0) {
      avio_closep(&ctx_->pb);
    }
    avformat_free_context(ctx_);
    ctx_ = nullptr;
  }

 private:
  AVFormatContext* ctx_ = nullptr;
  bool opened_ = false;
  bool header_written_ = false;
};

/// Where the output timeline has reached, and what to add to the next segment
/// so it continues from there.
///
/// Each segment is demuxed on its own and arrives with its own idea of where
/// zero is. Segments that already continue produce an offset of zero and are
/// left alone; one that restarts is shifted past what is already written.
///
/// One offset shared by every stream, deliberately: per-stream offsets would
/// let a segment's audio and video drift apart.
class Timeline {
 public:
  explicit Timeline(unsigned int stream_count) : streams_(stream_count) {}

  /// Call before each segment's packets. The offset is not computed here but at
  /// the first packet that actually carries a timestamp, since a segment can
  /// open with one that does not.
  void BeginSegment() {
    offset_resolved_ = false;
    offset_us_ = 0;
  }

  void Rewrite(AVPacket* packet, AVRational in, AVRational out) {
    StreamState& state =
        streams_[static_cast<std::size_t>(packet->stream_index)];

    const std::int64_t anchor =
        packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
    if (!offset_resolved_ && anchor != AV_NOPTS_VALUE) {
      const std::int64_t start_us = av_rescale_q(anchor, in, AV_TIME_BASE_Q);
      offset_us_ =
          wrote_any_ ? std::max<std::int64_t>(0, end_us_ - start_us) : 0;
      offset_resolved_ = true;
    }

    const std::int64_t offset = av_rescale_q(offset_us_, AV_TIME_BASE_Q, out);
    const std::int64_t duration =
        packet->duration > 0 ? av_rescale_q(packet->duration, in, out) : 0;

    std::int64_t dts =
        packet->dts != AV_NOPTS_VALUE
            ? av_rescale_q_rnd(packet->dts, in, out, AV_ROUND_NEAR_INF) + offset
            : AV_NOPTS_VALUE;
    std::int64_t pts =
        packet->pts != AV_NOPTS_VALUE
            ? av_rescale_q_rnd(packet->pts, in, out, AV_ROUND_NEAR_INF) + offset
            : AV_NOPTS_VALUE;

    // A packet with no decode timestamp is given one that continues this
    // stream, rather than passed through as AV_NOPTS_VALUE for the muxer to
    // reject. This is the other half of what the AVFMT_NOTIMESTAMPS write was
    // reaching for.
    if (dts == AV_NOPTS_VALUE) {
      dts = state.last_dts != AV_NOPTS_VALUE
                ? state.last_dts + std::max<std::int64_t>(1, duration)
                : 0;
    }
    if (pts == AV_NOPTS_VALUE || pts < dts) {
      pts = dts;
    }

    // Last resort, per stream: a segment boundary is not the only place a
    // duplicate decode timestamp can appear, and the muxer rejects the file
    // rather than the packet. Shifting pts by the same amount keeps the
    // reordering delay a B-frame stream depends on.
    if (state.last_dts != AV_NOPTS_VALUE && dts <= state.last_dts) {
      const std::int64_t bump = state.last_dts + 1 - dts;
      dts += bump;
      pts += bump;
    }

    state.last_dts = dts;
    packet->dts = dts;
    packet->pts = pts;
    packet->duration = duration;
    // The byte offset in the *input* file, which means nothing in the output.
    packet->pos = -1;

    end_us_ =
        std::max(end_us_, av_rescale_q(dts + duration, out, AV_TIME_BASE_Q));
    wrote_any_ = true;
  }

 private:
  struct StreamState {
    std::int64_t last_dts = AV_NOPTS_VALUE;
  };

  std::vector<StreamState> streams_;
  std::int64_t end_us_ = 0;
  std::int64_t offset_us_ = 0;
  bool wrote_any_ = false;
  bool offset_resolved_ = false;
};

}  // namespace

Result Remux(qsv::Reader& reader, const std::string& output_path,
             const ProgressCallback& on_progress) {
  Result result;
  if (!reader.ok()) {
    result.message = qsv::Describe(reader.status());
    return result;
  }

  AVPacket* packet = av_packet_alloc();
  if (packet == nullptr) {
    result.message = "av_packet_alloc";
    return result;
  }
  const std::unique_ptr<AVPacket, void (*)(AVPacket*)> packetGuard(
      packet, [](AVPacket* p) { av_packet_free(&p); });

  Output output;
  std::unique_ptr<Timeline> timeline;
  double reported = -1.0;

  for (int segment = 0; segment < reader.segment_count(); ++segment) {
    if (!reader.SeekToSegment(segment)) {
      result.message = qsv::Describe(reader.status());
      break;
    }

    SegmentInput input;
    if (!input.Open(reader, result.message)) {
      break;
    }

    if (segment == 0) {
      if (!output.Open(output_path, input.get(), result.message)) {
        break;
      }
      if (!output.WriteHeader(result.message)) {
        break;
      }
      timeline = std::make_unique<Timeline>(output.get()->nb_streams);
    }
    timeline->BeginSegment();

    int status = 0;
    while ((status = av_read_frame(input.get(), packet)) >= 0) {
      const int index = packet->stream_index;
      if (index < 0 || index >= static_cast<int>(output.get()->nb_streams)) {
        ++result.dropped_packets;
        av_packet_unref(packet);
        continue;
      }

      timeline->Rewrite(packet, input.get()->streams[index]->time_base,
                        output.get()->streams[index]->time_base);

      // Takes ownership and leaves the packet blank, so there is no unref to
      // pair with this — on the error path either.
      if (av_interleaved_write_frame(output.get(), packet) < 0) {
        result.message = "av_interleaved_write_frame";
        break;
      }

      const double progress = reader.progress();
      if (on_progress && progress - reported >= kProgressStep) {
        reported = progress;
        if (!on_progress(progress)) {
          result.cancelled = true;
          break;
        }
      }
    }

    if (!result.message.empty() || result.cancelled) {
      break;
    }
    // AVERROR_EOF is the expected end of a segment. Anything else is a demuxer
    // failure, and a reader that went bad underneath it is the more specific
    // explanation of the two, so it wins.
    if (status != AVERROR_EOF) {
      result.message =
          reader.ok() ? "av_read_frame" : qsv::Describe(reader.status());
      break;
    }
    if (!reader.ok()) {
      result.message = qsv::Describe(reader.status());
      break;
    }
  }

  if (result.message.empty() && !result.cancelled) {
    if (output.WriteTrailer(result.message)) {
      result.ok = true;
      if (on_progress) {
        on_progress(1.0);
      }
    }
  }

  if (!result.ok) {
    // Close before removing, so the muxer is not still holding the file, and
    // only remove one this run created: a failed avio_open() means the path
    // belongs to something else.
    const bool created = output.opened();
    output.Close();
    if (created) {
      std::remove(output_path.c_str());
    }
  }
  return result;
}

}  // namespace remux
