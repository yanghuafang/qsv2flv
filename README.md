# `qsv2flv`

[![CI](https://github.com/yanghuafang/qsv2flv/actions/workflows/ci.yml/badge.svg)](https://github.com/yanghuafang/qsv2flv/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Convert iQIYI **`.qsv`** downloads to **MP4** or **FLV**, without re-encoding
and without quality loss. Desktop application and command line, for **macOS**
and **Ubuntu**.

The `.qsv` format was reverse-engineered by
[btnkij](https://github.com/btnkij/qsv2flv), whose Windows application this
shares a name with; the code here was written for this project. The name is
historical — MP4 is the format you almost certainly want.

## Why this repository

A `.qsv` is not an encoding, it is a wrapper: several ordinary FLV or MPEG-TS
streams with an obfuscated first kilobyte each, behind an index table.
Converting one means unwrapping it and putting the packets in a container
players understand. No frame is decoded, so it runs at disk speed and the
picture is bit-identical.

- **One tree, both platforms.** Almost nothing here is platform-specific; Qt and
  FFmpeg abstract everything the program does. What differs is build
  configuration, and it lives in one `CMakeLists.txt`.
- **The core is testable.** `src/qsv/` has no Qt and no FFmpeg in it, so the
  container parser can be driven — and fuzzed — without a GUI toolkit or a media
  library in the process.
- **It has tests, and needs no sample files.** The suite generates its own
  `.qsv` fixtures, video and all. [How that works](docs/Testing.md).
- **The hard parts are written down.** Wrapping bounds checks, segment
  timestamps, FFmpeg's ownership rules — the places where the obvious code is
  wrong are in
  [docs/ImplementationNotes.md](docs/ImplementationNotes.md).

## How it works

```
file.qsv
  │  [src/qsv/]    validate the header and index table, then hand out each
  │                segment as a plain byte stream (first 0x400 bytes decrypted)
  ▼
FLV or MPEG-TS, one complete stream per segment
  │  [src/remux/]  demux each segment, copy its packets into one output
  │                container, rewriting only the timestamps
  ▼
file.mp4  /  file.flv
```

[docs/Architecture.md](docs/Architecture.md) walks the layers and maps them to
files; [docs/QsvFormat.md](docs/QsvFormat.md) is the container itself, offset by
offset.

## Quick start

```bash
git clone git@github.com:yanghuafang/qsv2flv.git
cd qsv2flv/scripts

# macOS:                ./install-deps-macos.sh
# Ubuntu: ./install-deps-ubuntu.sh

./build.sh
./run-tests.sh
```

Expected output ends with:

```text
100% tests passed, 0 tests failed out of 3
```

Then convert something:

```bash
../../qsv2flv-build/qsv2flv-cli -o ~/Movies ~/Downloads/*.qsv
```

or open the application:

```bash
../../qsv2flv-build/qsv2flv              # Linux
open ../../qsv2flv-build/qsv2flv.app     # macOS
```

Build output goes to `../qsv2flv-build/`, a sibling of the repository, so the
source tree stays clean.

### No `.qsv` to hand?

The test fixtures are generated, and the generator ships:

```bash
../../qsv2flv-build/qsv2flv-make-fixture /tmp/sample.qsv 3 30
../../qsv2flv-build/qsv2flv-cli /tmp/sample.qsv
```

## Using it

```
qsv2flv-cli [options] <file.qsv> ...

  -o, --output-dir DIR   Write output here (default: beside each input)
  -f, --format EXT       Container extension: mp4 (default) or flv
  -v, --verbose          Show FFmpeg's own diagnostics
  -h, --help             Show this help
```

In the application: add files with the button or by dropping them on the window,
pick a folder and a format, press **Convert**. The button becomes **Stop** while
a run is going.

**Prefer MP4.** FLV cannot carry H.265, which the higher-quality sources use, so
the muxer refuses those files rather than re-encoding them. Full reference and a
table of every failure message: [docs/Usage.md](docs/Usage.md).

## Repository layout

```
qsv2flv/
├── src/               # src/ is the only include root, so every include names
│   │                  # its directory ("qsv/reader.h", not "reader.h")
│   ├── qsv/                   # The container. No Qt, no FFmpeg.
│   │   ├── types.*            # on-disk layout; little-endian field decode
│   │   ├── crypto.*           # the two obfuscation schemes
│   │   └── reader.*           # validate, seek to a segment, read its bytes
│   ├── remux/                 # The conversion. FFmpeg, no Qt, no threads.
│   │   └── remuxer.*          # demux each segment, mux one output, fix timestamps
│   ├── gui/                   # Qt 6 desktop application
│   │   ├── main_window.*      # window, drag and drop, start/stop
│   │   ├── file_list_model.*  # QAbstractTableModel over the queue
│   │   ├── converter_thread.* # worker; never touches the model
│   │   └── input_file.*       # one row: path, state, progress
│   └── cli/main.cc            # headless converter
├── tests/             # Suites plus the fixture generators they run on
│   └── support/               # qsv_builder.* (synthetic .qsv), media.* (synthetic video)
├── scripts/           # build.sh, run-tests.sh, check-cli.sh, format.sh, tidy.sh
├── docs/              # Guides (start with docs/README.md)
├── CMakeLists.txt     # FFmpeg via pkg-config, Qt 6, sanitizer options
├── LICENSE            # MIT
└── NOTICE             # attribution for the format analysis
```

## Documentation

| Guide | Topics |
|-------|--------|
| [docs/Install.md](docs/Install.md) | Dependencies, `build.sh` options, manual CMake, troubleshooting |
| [docs/Usage.md](docs/Usage.md) | Both front ends, choosing a container, what each failure means |
| [docs/Testing.md](docs/Testing.md) | Generated fixtures, the suites, sanitizers, CI |
| [docs/Architecture.md](docs/Architecture.md) | The three layers, the file map, the timestamp handling |
| [docs/QsvFormat.md](docs/QsvFormat.md) | The `.qsv` container, offset by offset |
| [docs/ImplementationNotes.md](docs/ImplementationNotes.md) | Where the obvious implementation is wrong, and why |

## Requirements

**macOS** (Homebrew) or **Ubuntu**, with **FFmpeg 6.0+**,
**CMake 3.22+**, and a **C++17** compiler. **Qt 6.2+** for the desktop
application only — `./build.sh --preset cli-only` builds the CLI and the tests
without it.

FFmpeg sets the floor on its own — the CMake and Qt minimums are lower than
any release carrying FFmpeg 6 provides, so they never bind. 6.0 is a support
floor rather than an API one: nothing here needs an API newer than 4.4, but
4.x and 5.x are end-of-life and no CI runner carries them. See
[docs/Install.md](docs/Install.md#why-these-minimum-versions).

## Status

The converter works and is tested end to end against generated fixtures. What is
**not** verified is the thing no test in this repository can verify: behaviour
against a real `.qsv` written by a current iQIYI client. Everything here proves
the code does what [docs/QsvFormat.md](docs/QsvFormat.md) says; if that document
has drifted from the client, only a bug report will show it. Reports are
welcome — client version, video title and resolution are usually enough, and
`-v` output helps.

Known gaps are listed in
[docs/Testing.md § What is not covered](docs/Testing.md#what-is-not-covered).

## Contributing

Bug reports, fixes, tests and documentation are all welcome. See
[CONTRIBUTING.md](CONTRIBUTING.md) for the build, test and style workflow.

## License

MIT — see [LICENSE](LICENSE).

The reverse-engineering of the `.qsv` format that everything here rests on is
the work of [btnkij](https://github.com/btnkij/qsv2flv), credited in
[NOTICE](NOTICE). That project publishes no licence of its own, so the credit
is exactly that — a credit, not a claim about its terms. The code here is an
independent implementation; what it shares with the original is knowledge of a
file format.

Convert only what you are entitled to keep. This tool changes a container; it
does not grant any rights to what is inside it.
