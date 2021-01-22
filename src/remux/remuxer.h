#ifndef QSV2FLV_REMUX_REMUXER_H_
#define QSV2FLV_REMUX_REMUXER_H_

#include <cstdint>
#include <functional>
#include <string>

#include "qsv/reader.h"

/// \file
/// Packets are copied through unchanged; only the container and the timestamps
/// are rewritten. An output container that cannot carry the codec inside
/// (H.265 in FLV) therefore fails at the muxer rather than being re-encoded.
///
/// No Qt and no threading here, which is what lets the suite drive it in
/// process. See docs/Architecture.md.

namespace remux {

/// Fraction complete, in [0, 1]. Return false to cancel.
///
/// Runs on whichever thread called Remux(), not the GUI thread.
using ProgressCallback = std::function<bool(double)>;

struct Result {
  bool ok = false;

  /// A ProgressCallback asked to stop, and the partial output was removed.
  /// `ok` is false too, but nothing went wrong.
  bool cancelled = false;

  /// Empty when `ok`. Otherwise the failing FFmpeg entry point, or the
  /// qsv::Reader status — named after the call so it points at one line.
  std::string message;

  /// Packets whose stream index no segment after the first had a stream for.
  /// Non-zero means the segments disagreed about their stream layout: the file
  /// is playable but incomplete.
  int dropped_packets = 0;
};

/// Remux every segment of `reader`, in order, into one file at `output_path`.
/// The container comes from the extension. `reader` must already be ok().
Result Remux(qsv::Reader& reader, const std::string& output_path,
             const ProgressCallback& on_progress = {});

}  // namespace remux

#endif  // QSV2FLV_REMUX_REMUXER_H_
