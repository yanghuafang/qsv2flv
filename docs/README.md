# qsv2flv documentation

Guides for using and working on **qsv2flv**. The [root README](../README.md) has
the clone-and-build path; this page indexes everything else.

## How-to

| Document | Contents |
|----------|----------|
| [Install.md](Install.md) | Dependencies for macOS and Ubuntu, `build.sh` options, manual CMake, troubleshooting |
| [Usage.md](Usage.md) | The application and `qsv2flv-cli`, choosing a container, what each failure message means |
| [Testing.md](Testing.md) | **How a converter for a format you cannot ship samples of gets tested** — generated fixtures, the suites, sanitizers, CI |

## Reference

| Document | Contents |
|----------|----------|
| [Architecture.md](Architecture.md) | The three layers, the file map, and where to make a given change |
| [QsvFormat.md](QsvFormat.md) | The `.qsv` container: header, index table, segments, both obfuscation schemes |

## Implementation notes

| Document | Contents |
|----------|----------|
| [ImplementationNotes.md](ImplementationNotes.md) | Where the obvious implementation is wrong: the container, FFmpeg and Qt traps, and deliberate scope |

## Where to look first

| If you want to | Start with |
|----------------|------------|
| Convert a video | [Usage.md](Usage.md) |
| Build it | [Install.md](Install.md) |
| Understand the file format | [QsvFormat.md](QsvFormat.md) |
| Fix a bug in the converter | [Architecture.md § Remuxing](Architecture.md#remuxing) |
| Fix a bug in the container parser | [Architecture.md § Reading a container](Architecture.md#reading-a-container) |
| Add a test | [Testing.md](Testing.md), then [CONTRIBUTING.md](../CONTRIBUTING.md#adding-a-test) |
| Know why something is the way it is | [ImplementationNotes.md](ImplementationNotes.md) |
