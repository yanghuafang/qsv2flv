# Architecture

```
file.qsv
  │  [qsv/]      Reader — validate the header and index table, then hand out
  │              each segment as a plain byte stream
  ▼
FLV or MPEG-TS bytes, one stream per segment
  │  [remux/]    Remuxer — demux each segment, copy its packets into one output
  │              container, rewriting only the timestamps
  ▼
file.mp4 / file.flv

  [gui/]  Qt window, table model, worker thread   ─┐
  [cli/]  argument parsing, progress line          ─┴─► both call remux::remux()
```

## Layers

`src/` is the only include root, so every include names its directory —
`#include "qsv/reader.h"`, not `#include "reader.h"`. The layer a header
belongs to is then visible at each use site, which makes an unwanted edge
obvious in review. There are three, and the dependencies run one way:

| Directory | Depends on | Knows nothing about |
|-----------|------------|---------------------|
| [`src/qsv/`](../src/qsv) | the C++ standard library | FFmpeg, Qt |
| [`src/remux/`](../src/remux) | `qsv/`, FFmpeg | Qt, threads |
| [`src/gui/`](../src/gui), [`src/cli/`](../src/cli) | `remux/`, `qsv/` | — |

The two boundaries are load-bearing rather than tidy.

**`qsv/` has no FFmpeg in it** because what a segment contains is not the
reader's business — it hands out bytes, and `remux/` decides they are MPEG-TS.
That is also what lets the container parser be tested and fuzzed without a media
library anywhere near the process.

**`qsv/` has no Qt in it** — unlike the original, whose reader was built on
`QFile` and `QString`, so reading a container pulled in a GUI toolkit and could
not be tested without one. Nothing here needs more than `<fstream>` and
`std::string`.

**`remux/` has no threading in it.** `remux()` is a plain function with a
progress callback. The GUI runs it on a `QThread` and the CLI runs it inline;
neither arrangement is baked into the conversion.

## File map

| File | Responsibility |
|------|----------------|
| [`qsv/types.h`](../src/qsv/types.h) `.cc` | The on-disk layout: header and index-record offsets, little-endian field decode |
| [`qsv/crypto.h`](../src/qsv/crypto.h) `.cc` | The two obfuscation schemes, reproduced byte for byte |
| [`qsv/reader.h`](../src/qsv/reader.h) `.cc` | Validate a container; seek to a segment; read its decrypted bytes |
| [`remux/remuxer.h`](../src/remux/remuxer.h) `.cc` | Demux each segment, mux one output, rewrite timestamps |
| [`gui/input_file.h`](../src/gui/input_file.h) `.cc` | One row of the list: path, state, progress |
| [`gui/file_list_model.h`](../src/gui/file_list_model.h) `.cc` | `QAbstractTableModel` over those rows |
| [`gui/converter_thread.h`](../src/gui/converter_thread.h) `.cc` | Worker: runs jobs, reports by row number |
| [`gui/main_window.h`](../src/gui/main_window.h) `.cc` `.ui` | Window, drag and drop, start/stop |
| [`cli/main.cc`](../src/cli/main.cc) | Argument parsing, output naming, progress line |

## Reading a container

`qsv::Reader`'s constructor validates everything at once: signature, version,
index count, and every index record against the file's real length. After it
returns, `status()` is final and a failed reader stays failed — every later call
is a no-op. Callers therefore check once, at the top, rather than after each
step.

Two things it does that the original did not:

**The encrypted prefix is buffered whole at seek time.** The original decrypted
in place on the first read, applying the cipher to a fixed `0x400` bytes of
whatever buffer the caller passed, however few bytes had actually landed in it.
That was safe only because its one caller happened to hand over a 32 KiB FFmpeg
buffer; anyone else got a heap overflow. Buffering also means a caller can read
in 100-byte chunks, which is what `ReaderTest` does.

**Progress is derived, not accumulated.** `processed_size()` is "the segments
before this one, plus what has been served from this one", computed from a
prefix-sum table. Seeking backwards therefore reports where the reader is rather
than the high-water mark it reached earlier. The counters are `int64_t`; the
original held them in `int` and summed every segment into them, so a
feature-length file overflowed and the progress column went backwards.

## Remuxing

`remux::remux()` walks the segments in order. The first one decides the output:
its streams are copied into a new muxer with `avcodec_parameters_copy`, the
header is written, and every segment after that contributes packets to the same
file.

No decoder is ever asked for. `avformat_new_stream()` is passed `nullptr`, not
`avcodec_find_decoder()` — a remux copies packets, so requiring a decoder only
means refusing streams that could have been copied. That is what made the
original reject H.265 on an FFmpeg built without an HEVC decoder.

Every FFmpeg object is owned by a small RAII class (`SegmentInput`, `Output`),
which replaced a `goto` cleanup label. One of them exists for a reason worth
naming: `avformat_open_input()` sets `AVFMT_FLAG_CUSTOM_IO` when `pb` is already
populated, and `avformat_close_input()` then deliberately leaves that `pb` alone
— it belongs to the caller. The original never freed it, so each segment of each
conversion leaked an `AVIOContext` and its 32 KiB buffer.

### Joining segments

Each segment carries its own container header, so each is demuxed on its own and
arrives with its own idea of where zero is. In real `.qsv` files the timestamps
continue from the previous segment rather than restarting, so naively copying
them works — most of the time. When it does not, the muxer rejects the file:

```
Application provided invalid, non monotonically increasing dts to muxer
```

The original's answer was

```c
outCtx->oformat->flags |= AVFMT_NOTIMESTAMPS;
```

which writes through `oformat` into libavformat's own muxer descriptor. Those
descriptors have been `const` and in read-only memory since FFmpeg 5, so on
every version this project supports that line is a segfault rather than a
workaround. It was also aimed at the wrong thing — `AVFMT_NOTIMESTAMPS` is a
muxer capability bit meaning "this container has no timestamps", not a request
to stop checking them.

`Timeline` in [`remuxer.cc`](../src/remux/remuxer.cc) does it properly:

- It tracks where the output timeline has reached, in microseconds.
- At the first timestamped packet of each segment it computes one offset:
  `max(0, endSoFar - segmentStart)`. For a file whose segments already continue,
  that is **zero and nothing is changed**. For a segment that restarts at zero,
  it is exactly enough to place it after everything already written.
- The offset is shared by every stream, deliberately. A per-stream offset would
  let a segment's audio and video shift by different amounts and drift apart.
- A packet with no decode timestamp is given one that continues its stream,
  rather than passed through as `AV_NOPTS_VALUE` for the muxer to reject.
- A per-stream monotonicity guard catches the rest, shifting `pts` by the same
  amount as `dts` so a B-frame stream keeps its reordering delay.

`RemuxTest` asserts the resulting timestamps are strictly increasing across a
segment boundary, which is a case the fixtures can produce on demand and real
files mostly cannot.

**One caveat, stated plainly:** the offset logic is designed to be a no-op on
files whose segments already run continuously, but it has not been verified
against a real `.qsv` — see [Testing.md § What is not covered](Testing.md#what-is-not-covered).

## The GUI

Three classes and one rule: **the worker never touches the model.**

`MainWindow` resolves both paths for every queued file when Convert is pressed
and hands `ConverterThread` a list of plain `ConversionJob` values. The worker
reports back by row number over queued signals, and the slots that update the
model run on the GUI thread. Changing the output folder mid-run therefore cannot
affect a conversion already under way, and cannot race with it.

The original passed the worker a raw pointer into the model's own vector and
left Add Files enabled during a run, so enqueueing another file reallocated that
vector under the worker. It also parented the thread to the window while
connecting `finished` to `deleteLater`, so closing mid-conversion destroyed a
running `QThread` — a `qFatal`. `MainWindow::closeEvent()` now asks, stops, and
waits.

`FileListModel` is a real `QAbstractTableModel` with proper
`beginInsertRows`/`endInsertRows` and a targeted `dataChanged` for status
updates, rather than a `QStandardItemModel` rebuilt from scratch on every
change. A rebuild resets the model, which drops the user's selection and column
widths — and invalidates the `QModelIndex` values a caller is holding, which is
what made "select three rows and press Remove" delete the wrong ones.

## Build

One [`CMakeLists.txt`](../CMakeLists.txt). FFmpeg is found through **pkg-config**
so the headers and the libraries come from the same install — the macOS fork
this replaces committed FFmpeg 4.4's Windows headers, put them on the include
path, and linked whatever Homebrew's `ffmpeg` happened to be, which compiles and
links cleanly and then reads every struct field at the wrong offset.

Three libraries are asked for: `libavformat`, `libavcodec`, `libavutil`. The
original linked `avdevice`, `avfilter`, `swscale`, `swresample` and `postproc`
as well, none of which a remux touches.

`QSV2FLV_BUILD_GUI=OFF` drops Qt entirely, which is what the sanitizer CI job
and any headless machine use.
