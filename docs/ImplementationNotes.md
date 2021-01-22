# Implementation notes

Why parts of this code look the way they do. Most of it is unremarkable; these
are the places where the obvious version is wrong, and the reason is not
visible from the code alone.

## The traps in the container

**A wrapping bounds check.** The natural way to validate an index record is
`offset + size > file_size`. That wraps for an offset near the top of the
64-bit range: the sum comes out small, the record passes, and the seek that
follows is unbounded. [`reader.cc`](../src/qsv/reader.cc) checks by subtraction
instead, and [`ReaderTest`](../tests/reader_test.cc) builds a fixture that
wraps.

**Sizes do not fit in an `int`.** Segment sizes summed into a 32-bit counter go
negative somewhere past 2 GiB, which is an ordinary length for the
feature-length files this tool exists for. Every size and offset is `int64_t`,
and the progress arithmetic is a free function so
[a test can reach lengths no fixture can](Testing.md).

**The encrypted prefix is `0x400` bytes, not `0x400` bytes of your buffer.**
Decrypting a fixed `0x400` from a caller's buffer on the first read of a
segment overflows it whenever the caller reads in smaller chunks. The prefix is
buffered whole at seek time, so a caller can read in any size it likes —
`ReaderTest` reads in 100-byte chunks precisely to hold that.

**Zero-length input.** `DecryptV2` computes `size - 1` on an unsigned counter,
so a length of zero walks 4 GiB backwards off the front of the buffer. Guarded,
with a case in [`CryptoTest`](../tests/crypto_test.cc) that puts a canary
either side to prove the call writes nothing at all.

**An unrecognised version must stop parsing.** Setting an error code and
carrying on decrypts the index table under rules that do not apply to it.

## The traps in FFmpeg

**Never write through `oformat`.** `outCtx->oformat->flags |=
AVFMT_NOTIMESTAMPS` reaches into libavformat's own muxer descriptor. Those have
been `const` and in read-only memory since FFmpeg 5, so it is a segfault rather
than a workaround — and it is aimed at the wrong thing regardless:
`AVFMT_NOTIMESTAMPS` is a capability bit meaning "this container has no
timestamps", not a request to stop checking them. Segment timestamps are
handled by [`Timeline`](Architecture.md#joining-segments) instead.

**A remux needs no decoder.** Looking up `avcodec_find_decoder` to build the
output stream fails for a codec the local FFmpeg cannot decode — a stream it
would then have copied without decoding. An FFmpeg built without an HEVC
decoder would reject H.265 files it could have handled.
`avformat_new_stream(ctx, nullptr)` plus `avcodec_parameters_copy` is the whole
job.

**Custom I/O is yours to free.** `avformat_open_input` sets
`AVFMT_FLAG_CUSTOM_IO` when `pb` is already populated, and
`avformat_close_input` then deliberately leaves that `pb` alone. Freeing it is
the caller's, and it is `io_->buffer` that must be freed rather than the
pointer originally handed to `avio_alloc_context` — FFmpeg may have replaced
it.

**Headers and libraries must come from one install.** FFmpeg comes from
`pkg-config`. Vendored headers alongside a system library compile and link
cleanly and then read every struct field at the wrong offset, because
`AVFormatContext`'s layout moves between majors.

## The traps in Qt

**A worker may not hold a pointer into the model.** Appending to the list
reallocates it underneath a running conversion. The worker is handed plain
[`ConversionJob`](../src/gui/converter_thread.h) values and reports back by row
number over queued signals; the slots that touch the model run on the GUI
thread. Changing the output folder mid-run therefore cannot affect a conversion
already under way.

**Rebuilding a model invalidates the indices a caller is holding.** Removing
several selected rows by iterating the selection forwards uses row numbers that
go stale after the first removal. [`FileListModel`](../src/gui/file_list_model.h)
sorts and walks them high to low, and uses real `beginRemoveRows` /
`dataChanged` rather than clearing and rebuilding — a rebuild also drops the
user's selection and column widths.

**Destroying a running `QThread` is a `qFatal`.** `MainWindow::closeEvent()`
asks, stops the worker, and waits for it before the window goes away.

## Deliberate scope

**No transcoding.** This is a remux; a tool that re-encodes is `ffmpeg`. FLV
cannot carry H.265, so those files are refused rather than silently degraded.

**No Windows target.** Nothing here can test it, and claiming a platform that
nothing tests is how a header/library mismatch survives for a year.
`CMakeLists.txt` has no `win32` branch to rot.

**No standalone demo parsers.** Explaining the container is
[QsvFormat.md](QsvFormat.md)'s job. A second parser kept for teaching would be
a second parser to maintain, with none of the validation above.

**Cancellation, not just progress.** `Remux()` takes a callback that can stop
it, and removes the partial output when it does.

## Attribution

The format analysis in [QsvFormat.md](QsvFormat.md) is btnkij's work, and the
window title says so. That project publishes no licence file, so this is a
credit rather than a statement about its terms; [`NOTICE`](../NOTICE) says the
same at the root of the repository.
