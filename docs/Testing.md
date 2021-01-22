# Testing

All commands below assume `cd qsv2flv/scripts`.

```bash
./build.sh          # configure and build the release preset
./run-tests.sh      # every suite, through CTest, CliSmoke included
```

## The problem this suite had to solve

`qsv2flv` converts a proprietary container that nobody can redistribute. Its
inputs are downloaded television, so a fixture directory of real `.qsv` files
would be both a copyright problem and a hundred megabytes of binary in a
repository that is otherwise 3,000 lines of source. The original project had no
tests at all, and the reason is not hard to guess.

**Every fixture here is generated.** No sample video is checked in, downloaded,
or needed. That works because the format is documented
([QsvFormat.md](QsvFormat.md)) and, for version 1, fully constructible:

- the index table is stored in the clear, so a fixture can write real offsets;
- the segment prefix is obfuscated with an XOR, which is its own inverse, so a
  fixture can produce ciphertext from chosen plaintext by calling the same
  `DecryptV1` the reader will call;
- a segment is a complete, ordinary video stream, and FFmpeg — which this
  project already links against — can encode one.

[`tests/support/qsv_builder.*`](../tests/support/qsv_builder.h) does the first two.
[`tests/support/media.*`](../tests/support/media.h) does the third: it encodes
64x64 MPEG-2 video into an MPEG-TS stream in memory, which is the same shape the
newer iQIYI client puts inside a segment. MPEG-2 rather than H.264 because its
encoder is native to libavcodec and present in every build, so the suite does
not depend on whether the local FFmpeg was compiled with x264.

## Suites

| Suite | What it covers |
|-------|----------------|
| [`CryptoTest`](../tests/crypto_test.cc) | `DecryptV1` and `DecryptV2`, byte for byte |
| [`ReaderTest`](../tests/reader_test.cc) | Container parsing: one happy path, one fixture per way of being broken, and a pass of garbage |
| [`RemuxTest`](../tests/remux_test.cc) | End to end — build a `.qsv`, convert it, demux the result |

Run one by name:

```bash
./run-tests.sh ReaderTest
```

Each prints `NAME PASS (n checks)` or `NAME FAIL`, and a failure line carries
the file, the line, and both sides of the comparison. Failures do not stop the
run: one invocation should tell you every case that broke, not the first.

The harness is [`tests/support/assertions.h`](../tests/support/assertions.h)
— two macros and a counter. Deliberately not GoogleTest or Catch2: either would
be fetched at configure time, which turns `cmake ..` into a network operation
and makes a CI job's first failure mode a download rather than a compile.

### `RemuxTest` in detail

This is the one that would not exist without generated fixtures. It builds a
two-segment version 1 container out of two independently encoded clips, runs the
real `qsv::Reader` and `remux::remux()` over it, and then demuxes the MP4 that
comes out to ask four things:

1. It opens, has one stream, and that stream is still MPEG-2 — the packets were
   copied, not re-encoded.
2. It holds **exactly** as many packets as the two clips hold when demuxed on
   their own. The expected count is measured from the clips rather than written
   into the test, so the assertion is exact without being pinned to a particular
   FFmpeg's idea of how many packets 20 frames of MPEG-2 is.
3. Decode timestamps never go backwards. Both clips were encoded starting from
   zero, so a naive copy produces a second segment that collides with the first
   — this is the assertion the segment-joining logic in
   [Architecture.md](Architecture.md#joining-segments) exists to satisfy, and
   the one the original failed by writing `AVFMT_NOTIMESTAMPS` into
   libavformat's own muxer table.
4. The output runs for about as long as the two clips together, so the second
   segment really was appended rather than laid over the first.

It also checks that cancelling removes the half-written output, that an
unopenable input reports the reader's own status, and that an extension no muxer
answers to fails at `avformat_alloc_output_context2` rather than somewhere
further in.

If the local FFmpeg has no MPEG-2 encoder, the affected cases print
`skipped: this FFmpeg has no MPEG-2 encoder` and the suite still passes. That is
a property of the environment, not of the code under test.

### What version 2 gets instead

A version 2 container cannot be built. Its index records are stored under
`DecryptV2`, whose swap positions come from a state folded out of the
*ciphertext*, so producing a record that decrypts to a chosen offset and size
would mean solving for that state — a search that branches at every one of the
28 steps. There is no `encryptV2` to call, and pretending otherwise would be the
easiest way to ship a suite that looks thorough and tests nothing.

So version 2 is covered three ways instead, none of which needs a round trip:

- **Golden vectors** in `CryptoTest` pin `DecryptV2` at both lengths the reader
  calls it with. They were produced by running this implementation, so they
  cannot show it matches the client — only that it still matches itself, which
  is what a refactor of the rotate-and-swap loop needs.
- **A rejection fixture** in `ReaderTest`. `QsvBuilder` writes index records in
  the clear, which is correct for version 1 and wrong for version 2, so the
  reader decrypts plaintext and recovers noise. The offsets and sizes it gets
  are effectively random 64- and 32-bit values, and the contract is that every
  one is rejected by validation rather than seeked to. Under ASan that is a real
  check on the bounds arithmetic.
- **The garbage pass**, below.

### The garbage pass

`ReaderTest` ends by running 200 pseudo-random 4 KiB buffers through the parser,
each carrying a valid signature and, half the time, a version the reader
accepts — so the run gets past the early rejections and into the index table
with values no hand-written fixture would pick. Its own assertion is only
`CHECK(true)`: the point is what runs underneath it.

That is the arm the sanitizer job is really exercising. Every length in a `.qsv`
header is attacker-controlled if the file came off the internet, and this is
where an unvalidated `index_count`, a wrapping `offset + size`, or a read sized
from a corrupt field would show up as a diagnostic rather than as a silent
overrun. Two of the traps in [ImplementationNotes.md](ImplementationNotes.md) are
exactly that shape.

The generator is a small xorshift rather than `<random>`: the standard library's
engines are portable but its distributions are not, and a fixture that differs
between libstdc++ and libc++ would make a CI failure impossible to reproduce
locally.

## The command-line smoke test

Registered with CTest as `CliSmoke`, so it runs with everything else and
`ctest` locally checks what CI checks. To run it alone:

```bash
./run-tests.sh CliSmoke     # or: ctest --preset release -L smoke
./check-cli.sh              # the same three assertions, outside CTest
```

The suites drive `remux()` in process, which is the right place to assert on
behaviour but leaves one thing unchecked: whether the executable a user actually
runs starts, finds its shared libraries, and converts a file.
`qsv2flv-make-fixture` writes the in-memory fixture out to disk, and the script
then runs `qsv2flv-cli` over it and confirms there is a video stream in the
result. It also checks `--help` and that a missing input exits non-zero with a
message.

`qsv2flv-make-fixture` is built with the tests and never installed. It is also
the way to try the **GUI** without owning an iQIYI download:

```bash
../../qsv2flv-build/qsv2flv-make-fixture /tmp/sample.qsv 3 30
```

## Sanitizers

```bash
./build.sh     --preset ci-asan-ubsan
./run-tests.sh --preset ci-asan-ubsan
```

Both are opt-in CMake options (`QSV2FLV_ASAN`, `QSV2FLV_UBSAN`) applied to every
target. They are in this project rather than in a developer's private build
because its job is parsing an untrusted file format: the garbage pass above is
only an assertion of "did not crash", and ASan is what turns that into a real
check.

UBSan complements rather than repeats it — the shift-and-rotate arithmetic in
`qsv/crypto.cc` and the 64-bit offset checks in `qsv/reader.cc` are exactly
the shapes it reports on. It is built with `-fno-sanitize-recover`, so a finding
aborts the process and CI fails on it instead of printing and carrying on.
`vptr` is off: it needs RTTI on both sides of a cast and FFmpeg is C.

`--no-gui` is not required, but the sanitizer CI job uses it. Qt adds nothing
the sanitizers have anything to say about here, and leaving it out keeps the
build fast.

**LeakSanitizer is off** in CI, and that is a deliberate limitation rather than
an oversight. It would be useful — the `AVIOContext` this project used to leak
once per segment is exactly its kind of finding — but FFmpeg makes one-time
static allocations that it reports and that nothing here can free, and they
cannot be filtered out by module: `qsv2flv`'s own buffers come from `av_malloc`,
so a `leak:libavutil` suppression would hide this project's leaks along with
FFmpeg's. Turn it on by hand when changing object lifetimes in
`remux/remuxer.cc`, and read the report with that caveat in mind:

```bash
ASAN_OPTIONS=detect_leaks=1 ./run-tests.sh --preset ci-asan-ubsan
```

## Style gates

| Script | Purpose |
|--------|---------|
| `format.sh` | `clang-format` plus a trailing-whitespace strip; `--check` reports without writing |
| `tidy.sh` | `clang-tidy` against the curated list in `.clang-tidy`; `--fix` applies what it can |

Neither is a test, but CI runs both, so run them before pushing. Both gates run
on one platform: `clang-format` majors can disagree on layout, and three
reference versions would mean a gate that cannot be satisfied everywhere at
once. `format.sh` prints the binary and version it used, which is what makes a
disagreement diagnosable.

CI pins both tools to **version 18**, and pins them by package rather than by
relying on what a runner image happens to ship — `clang-format-18` and
`clang-tidy-18`, with `QSV2FLV_LLVM_PREFIX=/usr/lib/llvm-18`. A local pass with
a different major says nothing about the gate. To check the formatter for real
without touching your system tools:

```bash
python3 -m venv /tmp/cf && /tmp/cf/bin/pip install clang-format==18.1.8
QSV2FLV_LLVM_PREFIX=/tmp/cf ./scripts/format.sh --check
```

`QSV2FLV_LLVM_PREFIX` rather than `PATH`: on macOS `llvm-env.sh` resolves the
tools from the Homebrew keg and never consults `PATH`, so prepending to it
changes nothing and the gate quietly runs the wrong major.

## CI

Three workflows, each running on every push and pull request. They are separate
files rather than one, because their cadences have nothing in common: the style
gates answer in under a minute and need no compiler, while a build leg takes
minutes. Separate files also mean separate status checks and separate
concurrency groups.

| Workflow | Jobs | What it does |
|----------|------|--------------|
| [`lint.yml`](../.github/workflows/lint.yml) | `clang-format`, `clang-tidy` | the source gates: formatting, and the curated check list in `.clang-tidy` |
| [`build.yml`](../.github/workflows/build.yml) | `Ubuntu`, `macOS` | dependencies, `--preset ci-release`, `run-tests.sh` |
| [`sanitizers.yml`](../.github/workflows/sanitizers.yml) | `ASan + UBSan` | dependencies (no Qt), `--preset ci-asan-ubsan`, `run-tests.sh` |

`clang-tidy` builds the project itself rather than reusing `Build / Ubuntu`'s
tree: jobs do not share a filesystem, and it needs the compile database CMake
writes plus a Qt build, since it analyses all of `src/` including `src/gui`.
That costs about a minute, and buys a check you can see by name.

No job spells out a build setting: each names a preset from
[`CMakePresets.json`](../CMakePresets.json), which is the same thing you run
locally. `CliSmoke` is a registered test, so there is no separate step for the
shipped binary either.

Branch protection requires all five checks by name:

```
Lint / clang-format
Lint / clang-tidy
Build / Ubuntu
Build / macOS
Sanitizers / ASan + UBSan
```

Every one is named for a tool or a platform, so the list says what it enforces
without a lookup. The cost is that the list is manual: add a matrix leg or a
job and it is ungated until someone adds it here too, and remove one and a
required check waits forever for a report that will never come.

`fail-fast` is off. An FFmpeg API that moved between majors breaks exactly one
row of the matrix, and seeing which one is most of the diagnosis. The two rows
span the range on their own: a distribution carries one FFmpeg release for
years, near the floor `CMakeLists.txt` asks for, while Homebrew tracks the
newest.

`--werror` is used in CI and nowhere else. A compiler upgrade can add a warning
at any time, and someone who cloned this to convert a video should get a working
program rather than a wall of errors.

## What is not covered

Worth stating plainly, because generated fixtures buy a lot and not everything.

- **Real `.qsv` files.** Everything here proves the code does what
  [QsvFormat.md](QsvFormat.md) says. If the document is wrong about the client,
  or a newer client changes something, no test in this repository will notice.
  That is the one gap only a bug report against a real file can close.
- **Version 2 end to end.** See above.
- **FLV output.** The muxer is reached the same way MP4's is, but the suite only
  asserts on MP4; an FLV-specific muxer restriction would not be caught here.
- **The GUI.** No widget test. The model, the worker, and the window are thin by
  design — every decision they make is in `qsv2flv_core`, which is what the
  suites drive.
