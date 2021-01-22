#!/bin/bash

# build-env.sh — toolchain paths and build locations for every other script here.
#
# Sourced, never executed. Points pkg-config at FFmpeg and CMake at Qt 6,
# failing with an actionable message rather than letting a later script fail
# obscurely, and derives each preset's build directory in one place.
#
# This is the half of the build that CMakePresets.json cannot express. Homebrew
# installs Qt keg-only, so CMAKE_PREFIX_PATH has to come from a `brew --prefix`
# evaluated at run time and no JSON file can produce one. The division is:
# presets own every build setting, this owns the environment they are
# configured in.
#
# No `set -euo pipefail`: shell options are not scoped to a sourced file, so
# setting them here would change the caller's shell too.

qsv2flv_fail_env() {
  echo "$1" >&2
  return 1
}

case "$(uname -s)" in
  Darwin)
    if ! command -v brew >/dev/null 2>&1; then
      qsv2flv_fail_env "Homebrew is required on macOS. See docs/Install.md."
      return 1
    fi

    # Homebrew's ffmpeg is not keg-only, so its .pc files are symlinked into
    # the prefix — but only if `brew link` ran, and a --HEAD or pinned install
    # can leave them behind. Asking the formula where it lives is what makes
    # this work in both cases.
    if ffmpeg_prefix="$(brew --prefix ffmpeg 2>/dev/null)" &&
       [[ -d "${ffmpeg_prefix}/lib/pkgconfig" ]]; then
      export PKG_CONFIG_PATH="${ffmpeg_prefix}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    fi

    # Qt *is* keg-only, so CMake will not find Qt6Config.cmake without being
    # told. CMAKE_PREFIX_PATH rather than PATH: it is the CMake package search
    # that needs it, not the shell.
    if qt_prefix="$(brew --prefix qt 2>/dev/null)" && [[ -d "${qt_prefix}" ]]; then
      export CMAKE_PREFIX_PATH="${qt_prefix}:${CMAKE_PREFIX_PATH:-}"
    fi
    ;;
  Linux)
    # apt installs both into the default search paths, so there is nothing to
    # add. The check below is the whole contribution: it turns "no such package
    # libavformat" three layers into a CMake run into one line here.
    ;;
  *)
    qsv2flv_fail_env "Unsupported OS: $(uname -s). Supported: macOS, Ubuntu."
    return 1
    ;;
esac

if ! pkg-config --exists libavformat libavcodec libavutil 2>/dev/null; then
  qsv2flv_fail_env "FFmpeg development files not found by pkg-config.
  macOS:  ./install-deps-macos.sh   (brew install ffmpeg)
  Ubuntu: ./install-deps-ubuntu.sh  (apt install libavformat-dev ...)"
  return 1
fi

# BASH_SOURCE rather than $0, since this file is sourced and $0 is the caller's
# name.
qsv2flv_env_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Assigned before it is exported, not in one statement: `export VAR="$(cmd)"`
# takes export's exit status, so a failing cd is swallowed (SC2155).
QSV2FLV_REPO_DIR="$(cd "${qsv2flv_env_dir}/.." && pwd)"
export QSV2FLV_REPO_DIR

# What a script builds when it is not told otherwise. Named here so the answer
# is the same in build.sh, run-tests.sh and tidy.sh.
export QSV2FLV_DEFAULT_PRESET="release"

# Where a preset configures into, kept in step with `binaryDir` in
# CMakePresets.json: a sibling of the repo, so the source tree stays clean, and
# one directory per preset, so switching between a release build and a
# sanitizer build does not thrash a single CMake cache.
#
# There is no QSV2FLV_BUILD_DIR any more. A preset's binaryDir is fixed when it
# is configured, and an environment variable that moved it would only let the
# scripts and CMake disagree about where the build is. CMakeUserPresets.json is
# the preset-native replacement: inherit a preset there and override binaryDir.
qsv2flv_build_root="$(cd "${QSV2FLV_REPO_DIR}/.." && pwd)/qsv2flv-build"

qsv2flv_build_dir() {
  echo "${qsv2flv_build_root}/$1"
}
