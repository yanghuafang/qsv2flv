# Contributing to `qsv2flv`

Thanks for your interest. Contributions of every size are welcome: bug reports
against real files, fixes, new tests, and clearer documentation.

The most valuable contribution is the one this project cannot make for itself.
Every test here runs against generated fixtures ([docs/Testing.md](docs/Testing.md)),
which proves the code does what [docs/QsvFormat.md](docs/QsvFormat.md) says —
and nothing about whether that document still matches what the iQIYI client
writes. **A report from a real `.qsv` that fails to convert is worth more than
most patches.**

## Reporting a conversion failure

Please include:

- the exact message, from `qsv2flv-cli -v` if you can run it;
- the client version, the video title, and the resolution;
- whether other files from the same download work.

Please do **not** attach the `.qsv` itself. If the failure is in the container
parser rather than the muxer, the first `0x5A` bytes plus the index table are
usually enough, and are not video.

A message naming an FFmpeg function (`avformat_write_header`,
`av_interleaved_write_frame`) is a fault in this tool or an unsupported input. A
message from the table in [docs/Usage.md](docs/Usage.md#what-a-failure-means)
usually means the download itself is at fault — except
`Unsupported QSV version` and `The QSV index table does not describe this file`,
which can mean the format moved and are always worth reporting.

## Prerequisites

**macOS** (Homebrew) or **Ubuntu**, with FFmpeg 6.0+, CMake 3.22+, a C++17
compiler, and Qt 6.2+ for the desktop application.

```bash
cd scripts
./install-deps-macos.sh     # macOS
./install-deps-ubuntu.sh    # Ubuntu
```

Details and troubleshooting: [docs/Install.md](docs/Install.md).

## Build and test

```bash
cd scripts
./build.sh
./run-tests.sh
./check-cli.sh
```

Output goes to `../../qsv2flv-build/`, a sibling of the repo.

Before opening a pull request, run what CI runs:

```bash
./format.sh --check
./build.sh      --preset ci-release
./run-tests.sh  --preset ci-release   # includes CliSmoke
./tidy.sh       --preset ci-release

# and the job that catches what the assertions cannot
./build.sh --preset ci-asan-ubsan && ./run-tests.sh --preset ci-asan-ubsan
```

The sanitizer pass matters more here than the check count suggests. This program
parses an untrusted file format, and `ReaderTest`'s garbage pass asserts only
"did not crash" — ASan is what turns that into a real check. Two of the traps
in [docs/ImplementationNotes.md](docs/ImplementationNotes.md) are exactly that
shape.

## Adding a test

Suites live in `tests/` and are registered in the `foreach` near the bottom of
[`CMakeLists.txt`](CMakeLists.txt). Each is a plain executable that returns
`test::Finish("Name")`.

For a new case in an existing suite, add a function, call `test::BeginCase()` at
the top of it so failures are locatable, and call it from `main()`.

Build the fixture rather than checking one in:

- [`tests/support/qsv_builder.*`](tests/support/qsv_builder.h) writes a version 1
  container from chosen payload bytes, with a setter per way of being broken.
- [`tests/support/media.*`](tests/support/media.h) encodes real video into an
  MPEG-TS stream in memory, and summarises what a demuxer sees in a result.
- Use `test::PseudoRandomBytes()` rather than `<random>`: the standard library's
  distributions are not portable, and a fixture that differs between libstdc++
  and libc++ makes a CI failure impossible to reproduce locally.

Version 2 containers cannot be built — see
[docs/Testing.md § What version 2 gets instead](docs/Testing.md#what-version-2-gets-instead)
before writing a test that assumes they can.

If a case depends on something an FFmpeg build may not have, report a skip and
return rather than failing. That is a property of the environment, not of the
code under test; `RemuxTest` does this for the MPEG-2 encoder.

## Coding style

- **C++17**, formatted with `clang-format` using the repo's
  [`.clang-format`](.clang-format) — Google base style, 2-space indent, 80
  columns. On macOS both `clang-format` and `clang-tidy` come from one
  Homebrew keg, `brew install llvm`, and **Xcode's copies are not used** —
  see [docs/Install.md](docs/Install.md#the-style-tools-on-macos) for why.
- Naming follows the [Google C++ Style Guide][gcpp] rather than Qt's
  conventions, throughout:

  [gcpp]: https://google.github.io/styleguide/cppguide.html

  | Thing | Form | Example |
  |-------|------|---------|
  | Files | `snake_case.cc` / `.h` | `file_list_model.cc` |
  | Header guards | `QSV2FLV_<PATH>_<FILE>_H_` | `QSV2FLV_QSV_READER_H_` |
  | Types | `PascalCase` | `class SegmentInput` |
  | Functions | `PascalCase` | `SeekToSegment()` |
  | Accessors | `snake_case`, like the value | `total_size()` |
  | Variables, parameters | `snake_case` | `table_size` |
  | Class members | `snake_case_` | `prefix_consumed_` |
  | Struct members | `snake_case`, no underscore | `segment_offset` |
  | Constants, enumerators | `kPascalCase` | `kIndexSize`, `Status::kOk` |

- **`src/gui/` is the one exception, and only where Qt forces it.** A function
  that overrides a Qt virtual has to match the name it overrides, or it
  silently stops overriding anything — so `rowCount()`, `columnCount()`,
  `data()`, `headerData()`, `run()`, `dragEnterEvent()`, `dropEvent()` and
  `closeEvent()` keep Qt's `camelCase`. The same goes for the widget names in
  `main_window.ui`, which Qt Designer generates. Everything else in that
  directory follows the table above; when adding an override, leave a comment
  naming the base class that pins the spelling.

  Note the trap this creates: `QFileInfo::fileName()` is Qt's and stays
  `fileName()`, while `InputFile::file_name()` is ours and does not. A
  find-and-replace across the tree will get this wrong.

- Run the gates before committing:

  ```bash
  ./scripts/format.sh   # clang-format + strip trailing whitespace
  ./scripts/tidy.sh     # clang-tidy; --fix applies what it can
  ```

- The check list in [`.clang-tidy`](.clang-tidy) is curated, not the full
  upstream set, and each disabled family carries its reason. If a check would
  help, re-argue it there rather than silencing findings case by case.
- Comments explain **intent and trade-offs**, not what the line does. The ones
  worth writing here are about the file format, the FFmpeg ownership rules, and
  why an obvious-looking alternative was rejected — that is what
  [docs/ImplementationNotes.md](docs/ImplementationNotes.md) is for.
- Keep the layer boundaries: `qsv/` takes no dependency on FFmpeg or Qt, and
  `remux/` takes none on Qt or threads. They are what make the suite possible
  ([docs/Architecture.md](docs/Architecture.md#layers)).
- One idea per pull request.

## Commit messages

**Conventional Commits** — `type(scope): description`, lowercase, imperative, no
trailing period:

```
fix(reader): reject an index offset that wraps the 64-bit range
test(remux): pin the packet count against the segments demuxed alone
docs(format): record that version 2 index records are encrypted
```

Types in use: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`.

## Scope

Deliberately **out of scope**: transcoding (this is a remux — a tool that
re-encodes is `ffmpeg`), downloading, DRM of any kind, and Windows, which the
[original project](https://github.com/btnkij/qsv2flv) already supports and which
nothing here can test.

The gaps that *are* open are listed in
[docs/Testing.md § What is not covered](docs/Testing.md#what-is-not-covered).
Please open an issue before starting anything large.

## License

By contributing you agree that your contributions are licensed under the
project's [MIT License](LICENSE).

Do not paste code from the original project, or from any other decoder, into
this one. It carries no licence, so there is no permission to copy it — and
nothing here needs it: [docs/QsvFormat.md](docs/QsvFormat.md) states the format
as fact, which is what an independent implementation is written from. If you
learn something new about the container, add it there.
