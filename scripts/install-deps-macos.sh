#!/bin/bash

# install-deps-macos.sh — install qsv2flv's build dependencies via Homebrew.
#
# cmake, pkg-config, ffmpeg, qt and llvm. Qt is only needed for the desktop
# app; a machine that will only ever run qsv2flv-cli can skip it with
# QSV2FLV_WITH_GUI=0 and configure with -DQSV2FLV_BUILD_GUI=OFF. llvm is only
# needed to run the style gates; skip it with QSV2FLV_WITH_LINT=0.

set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
  echo "This script installs dependencies on macOS via Homebrew." >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew not found; installing it from https://brew.sh ..."
  # NONINTERACTIVE so the installer does not block on a RETURN keypress.
  NONINTERACTIVE=1 /bin/bash -c \
    "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
  # The installer does not touch the current shell's PATH; add brew for the
  # rest of this script (Apple Silicon = /opt/homebrew, Intel = /usr/local).
  if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ -x /usr/local/bin/brew ]]; then
    eval "$(/usr/local/bin/brew shellenv)"
  fi
fi

packages=(cmake pkg-config ffmpeg)
if [[ "${QSV2FLV_WITH_GUI:-1}" == 1 ]]; then
  packages+=(qt)
fi

# One keg supplies both clang-format and clang-tidy. The Command Line Tools do
# ship a clang-format, but this project does not use it: its version follows
# whatever Xcode is installed, and pinning one LLVM for both tools is the
# difference between a reproducible gate and one that depends on the machine.
# scripts/llvm-env.sh is where that choice lives.
#
# It is the asymmetry with Ubuntu, where both tools are one 50 MB apt package:
# Apple ships no clang-tidy at all, so on macOS this is a ~1.8 GB keg. Hence
# QSV2FLV_WITH_LINT, and hence the macOS CI job setting it to 0 -- both style
# gates run on ubuntu-latest only.
#
# Unversioned llvm rather than a pinned llvm@NN, matching the unversioned apt
# packages on the Ubuntu side. scripts/llvm-env.sh sets out why the drift is
# safe here and what the one real constraint is (clang-tidy 19+, which it
# enforces by version rather than by pin).
if [[ "${QSV2FLV_WITH_LINT:-1}" == 1 ]]; then
  packages+=(llvm)
fi

brew install "${packages[@]}"

echo
echo "macOS dependencies installed. Build with: ./build.sh"
if [[ "${QSV2FLV_WITH_LINT:-1}" != 1 ]]; then
  echo "Skipped llvm (QSV2FLV_WITH_LINT=0);"
  echo "./format.sh and ./tidy.sh will not run."
fi
