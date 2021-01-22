# Install

`qsv2flv` targets **macOS** (Homebrew) and **Ubuntu**. It
needs CMake 3.22+, a C++17 compiler, **FFmpeg 6.0 or newer**, and — for the
desktop application only — **Qt 6.2 or newer**.

## Dependencies

```bash
cd qsv2flv/scripts
./install-deps-macos.sh     # macOS
./install-deps-ubuntu.sh    # Ubuntu
```

Set `QSV2FLV_WITH_GUI=0` before either to skip Qt on a machine that will only
run `qsv2flv-cli`, and `QSV2FLV_WITH_LINT=0` to skip the style tools.

| | macOS | Ubuntu |
|---|---|---|
| Build | `cmake`, `pkg-config` | `build-essential`, `cmake`, `pkg-config` |
| FFmpeg | `ffmpeg` | `libavformat-dev`, `libavcodec-dev`, `libavutil-dev` |
| GUI | `qt` | `qt6-base-dev` |
| Style gates | `llvm` (both tools) | `clang-format`, `clang-tidy` |

### The style tools on macOS

Both come from **one Homebrew keg, `llvm`**, and Xcode's are deliberately not
used:

```bash
brew install llvm        # or ./install-deps-macos.sh
```

The Command Line Tools do ship a `clang-format`, but its version follows
whatever Xcode is installed: "it formats correctly here" would be a fact about
one machine rather than a property of the repository. One LLVM for both tools
is one moving part instead of two.

That keg is the asymmetry with Linux, where both tools are one small apt
package. Apple ships no `clang-tidy` at all, so on macOS this is a large
download — which is what `QSV2FLV_WITH_LINT=0` is for, and why the macOS CI job
sets it.

**No version is pinned**, on either platform. A pin is a maintenance debt that
comes due when the formula is retired, and nothing here needs one:
[`.clang-tidy`](../.clang-tidy) starts from `-*` and names every check it
wants, so a different analyser cannot introduce a check nobody has seen.

The one hard floor is not about checks: **`clang-tidy` must be 19 or newer on
macOS**, because the SDK's libc++ uses `__builtin_clzg`, a Clang 19 builtin. An
older one fails inside `<algorithm>` before a single check runs, which reads
like a broken checkout rather than a tool that is too old, so `tidy.sh` checks
the version and says so.

The keg is keg-only, so nothing lands on `PATH`;
[`scripts/llvm-env.sh`](../scripts/llvm-env.sh) resolves it by formula name, and
falls back to a versioned `llvm@NN` so a machine that already has one keeps
working. Two overrides live there:

| Variable | Effect |
|---|---|
| `QSV2FLV_LLVM_VERSION` | Pin to a keg version on macOS, e.g. `20` for `llvm@20` |
| `QSV2FLV_LLVM_PREFIX` | Use the LLVM install rooted here, on either platform |

### Why these minimum versions

| | Minimum | Why |
|---|---|---|
| FFmpeg | 6.0 (`libavformat` 60) | Oldest CI exercises; 4.x and 5.x are end-of-life |
| Qt | 6.2 | First Qt 6 LTS, and the oldest packaged anywhere this targets |
| CMake | 3.22 | What the build actually uses; nothing needs more |

**FFmpeg 6.0 is a support floor, not an API one.** The sources compile clean
against FFmpeg 4.4 headers — nothing here needs an API newer than that, and
the one place the API did move is guarded (`LIBAVFORMAT_VERSION_MAJOR >= 61`,
in `tests/support/media.cc`, for the `const` write callback FFmpeg 7.0
introduced). The floor is 6.0 because that is the oldest FFmpeg any CI runner
carries, and because 4.x and 5.x no longer receive security fixes, which
matters for a program whose input is a file off the internet. Lower it only
alongside a job that actually runs the older version.

The practical effect is that FFmpeg sets the distribution floor on its own:
the CMake and Qt minimums are lower than any release carrying FFmpeg 6
provides, so they never bind. They are the genuine minimums the code needs,
kept low because there is no reason to raise them.

`install-deps-ubuntu.sh` checks the FFmpeg the archive actually provides and
warns; adding a third-party apt source to reach the floor is not something a
setup script should do to someone's machine.

## Build

```bash
cd qsv2flv/scripts
./build.sh
```

Output goes to `../../qsv2flv-build/` — a sibling of the repository, so the
source tree stays clean.

```
qsv2flv-build/
├── qsv2flv                  # the desktop application (qsv2flv.app on macOS)
├── qsv2flv-cli              # the command-line converter
├── qsv2flv-make-fixture     # writes a synthetic .qsv, for testing
├── CryptoTest ReaderTest RemuxTest
└── compile_commands.json    # for clang-tidy and clangd
```

### Presets

Build settings live in [`CMakePresets.json`](../CMakePresets.json), not in
`build.sh` flags. `cmake --list-presets` enumerates them, which is the point:
the set of configurations this project supports is a list you can print rather
than argument-parsing code you have to read.

| Preset | What it is |
|--------|------------|
| `release` | GUI, CLI and tests, `Release`. The default. |
| `cli-only` | `release` without Qt, for a headless machine |
| `asan-ubsan` | `Debug`, no Qt, AddressSanitizer and UndefinedBehaviorSanitizer |
| `ci-release` | `release` plus `-Werror` — exactly what the build job runs |
| `ci-asan-ubsan` | `asan-ubsan` plus `-Werror` — exactly what the sanitizer job runs |

Each configures into its own directory, `../qsv2flv-build/<preset>/`, so
switching between them does not thrash one CMake cache.

### `build.sh` options

| Flag | Effect |
|------|--------|
| `--preset NAME` | Which preset to build (default: `release`) |
| `--clean` | Delete this preset's build directory first |

| Variable | Meaning |
|----------|---------|
| `QSV2FLV_BUILD_JOBS` | Parallel jobs (default: one per logical core) |

To build somewhere else, add a `CMakeUserPresets.json` that inherits a preset
and overrides `binaryDir`. There is no `QSV2FLV_BUILD_DIR`: a preset's
`binaryDir` is fixed when it is configured, and an environment variable that
moved it would only let the scripts and CMake disagree about where the build
is.

## Without the scripts

```bash
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The only thing [`build-env.sh`](../scripts/build-env.sh) adds is finding
Homebrew's keg-only Qt and, if `brew link` never ran, Homebrew's FFmpeg
`.pc` files. On macOS without it:

```bash
export CMAKE_PREFIX_PATH="$(brew --prefix qt):${CMAKE_PREFIX_PATH}"
export PKG_CONFIG_PATH="$(brew --prefix ffmpeg)/lib/pkgconfig:${PKG_CONFIG_PATH}"
```

The options the presets set, if you would rather pass them by hand:
`QSV2FLV_BUILD_GUI`, `QSV2FLV_BUILD_TESTS`, `QSV2FLV_ASAN`, `QSV2FLV_UBSAN`,
`QSV2FLV_WERROR`.

## Install

```bash
cmake --install ../../qsv2flv-build --prefix ~/.local
```

`qsv2flv-cli` goes to `bin/`; on macOS the bundle goes to the prefix root.

## Troubleshooting

**`FFmpeg development files not found by pkg-config`** — the packages above are
missing, or on macOS `brew link ffmpeg` never ran. `pkg-config --modversion
libavformat` should print 60 or higher.

**`Imported target "PkgConfig::FFMPEG" includes non-existent path`** — a
Homebrew upgrade moved FFmpeg to a new Cellar directory while the CMake cache
still named the old one. `build.sh` detects this and reconfigures by itself; if
you are driving CMake directly, delete the build directory. New configures pin
the stable `opt/` symlink instead, so this only affects caches created before
that change.

**`Could NOT find Qt6`** — install `qt` / `qt6-base-dev`, or build without the
GUI: `./build.sh --preset cli-only`.

**A `.app` that only runs on this machine.** The bundle CMake produces is not
signed and not deployed: the binary still refers to Qt and FFmpeg where Homebrew
put them, so it will not start on a machine that does not have them at the same
paths. Making it portable means `macdeployqt qsv2flv.app` to copy the Qt
frameworks in, `install_name_tool` for the FFmpeg dylibs, and a Developer ID
signature for Gatekeeper. None of that is wired up here — it needs a signing
identity, which CI does not have and this repository should not carry.

**Qt 5.** Not supported, and not a small gap: `FileListModel` is written against
Qt 6's `QAbstractTableModel` and container API. Every supported Ubuntu release
and Homebrew all package Qt 6.
