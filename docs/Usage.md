# Usage

Two front ends over the same converter. Neither re-encodes anything: a `.qsv`
holds ordinary FLV or MPEG-TS streams, and converting one means putting those
packets into a container that players understand. It runs at disk speed and the
picture is bit-identical.

## Choosing a container

**Use MP4 unless you have a reason not to.** FLV is offered because the original
tool was named after it, but it cannot carry H.265, which is what the
higher-quality sources use, and the muxer will refuse the file rather than
re-encode it into something it can carry. MP4 plays on essentially everything.

## The desktop application

```bash
../../qsv2flv-build/qsv2flv            # Linux
open ../../qsv2flv-build/qsv2flv.app   # macOS
```

Add files with the button or by dropping them on the window, pick an output
folder and a format, and press **Convert**. The button becomes **Stop** while a
run is going; stopping removes the file being written, since a half-written MP4
has no index and plays as corrupt rather than short. Anything not yet converted
stays in the list, so pressing Convert again picks up where it left off.

The list is frozen during a run — adding, removing, or clearing would change the
queue underneath the worker. Rows that finished show `Done`; rows that failed
show `Failed: <reason>`, and the reasons are the ones in
[the status table](#what-a-failure-means).

To try it without an iQIYI download, generate a fixture
([Testing.md](Testing.md)):

```bash
../../qsv2flv-build/qsv2flv-make-fixture /tmp/sample.qsv 3 30
```

## The command line

```
qsv2flv-cli [options] <file.qsv> ...

  -o, --output-dir DIR   Write output here (default: beside each input)
  -f, --format EXT       Container extension: mp4 (default) or flv
  -v, --verbose          Show FFmpeg's own diagnostics
  -h, --help             Show this help
```

```bash
qsv2flv-cli episode.qsv                       # -> episode.mp4, same folder
qsv2flv-cli -o ~/Movies *.qsv                 # a whole download folder
qsv2flv-cli -f flv -o /tmp old-client.qsv     # only if the source is H.264
```

Each converted file's path is printed on stdout and each failure on stderr, so a
batch can be piped:

```bash
qsv2flv-cli -o ~/Movies ~/Downloads/*.qsv > converted.txt
```

The exit status is 0 only if every input succeeded. Progress is drawn on one
rewritten line when stderr is a terminal, and suppressed otherwise — redirected
to a log, a carriage return produces one enormous line.

`--verbose` restores FFmpeg's own logging, which is at `AV_LOG_ERROR` by
default. It prints a stream dump per segment, so a feature-length file produces
dozens; it is the right flag when a conversion fails and you want to see what
the demuxer made of a segment.

## What a failure means

| Message | Meaning |
|---------|---------|
| `Cannot open the file` | Not there, or not readable |
| `Not a QSV file (no QIYI VIDEO signature)` | Wrong file, or the download never finished |
| `Unsupported QSV version` | A version this tool does not know — please open an issue with the client version |
| `The file ends before the container does` | Truncated download |
| `The QSV index table does not describe this file` | The index table decoded to offsets that are not in this file. Usually a corrupt download; on a version 2 file it can also mean the obfuscation changed |
| `Read error` | I/O failure part-way through |
| `avformat_write_header`, `av_interleaved_write_frame`, ... | The container was read fine and the muxer refused something. `-f flv` on an H.265 source is the common one; try `mp4` |
| `avformat_alloc_output_context2 (unknown container extension)` | `-f` was given something no muxer answers to |

Anything naming an FFmpeg function is a fault in this tool or an unsupported
input, not a broken download — those are worth reporting, with the output of
`-v`.

A conversion that succeeds may still warn:

```
warning: skipped N packet(s) whose stream is missing from the first segment
```

The output is built from the first segment's stream layout, so a later segment
with an extra stream has nowhere to put it. The file plays; something is
missing. Worth an issue.

## Comparing against `ffmpeg`

When a conversion fails and you want to know whether the fault is here, the
shortest test is to point FFmpeg's own tools at the same file. They will not
read a `.qsv` — the segments are obfuscated — but if `qsv2flv-cli` produced an
output at all, `ffprobe` on it says whether the result is well-formed:

```bash
ffprobe -v error -show_format -show_streams out.mp4
```
