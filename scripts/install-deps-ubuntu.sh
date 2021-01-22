#!/bin/bash

# install-deps-ubuntu.sh — install qsv2flv's build dependencies via apt.
#
# Targets any Ubuntu whose archive carries FFmpeg 6 or newer and Qt 6. An older
# release is warned about rather than worked around: adding a third-party apt
# source to reach the floor CMakeLists.txt asks for is not something a setup
# script should do to someone's machine. The check is on the FFmpeg found, not
# on a release number -- see below.
#
# Set QSV2FLV_WITH_GUI=0 to skip Qt on a headless box that only needs the CLI,
# and QSV2FLV_WITH_LINT=0 to skip clang-format and clang-tidy.

set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
  echo "This script installs dependencies on Ubuntu via apt." >&2
  exit 1
fi


export DEBIAN_FRONTEND=noninteractive
apt_get() {
  if [[ "$(id -u)" -eq 0 ]]; then
    apt-get "$@"
  else
    sudo -E apt-get "$@"
  fi
}

apt_get update

packages=(
  build-essential
  cmake
  pkg-config
  libavformat-dev
  libavcodec-dev
  libavutil-dev
)
# Same gate as install-deps-macos.sh, so one variable covers both platforms.
# Here they are two small apt packages rather than a large LLVM keg, so there
# is no reason to skip them except in a CI job that does not lint.
if [[ "${QSV2FLV_WITH_LINT:-1}" == 1 ]]; then
  packages+=(clang-format clang-tidy)
fi
if [[ "${QSV2FLV_WITH_GUI:-1}" == 1 ]]; then
  packages+=(qt6-base-dev)
fi

apt_get install -y --no-install-recommends "${packages[@]}"

# Check the constraint rather than the release number. An allow-list of Ubuntu
# versions goes stale every two years and says nothing about a derivative
# distribution; what actually matters is the FFmpeg the archive carries, and
# that is one question pkg-config can answer. libavformat 60 is FFmpeg 6.0,
# which is what CMakeLists.txt requires.
if libavformat_version="$(pkg-config --modversion libavformat 2>/dev/null)"; then
  libavformat_major="${libavformat_version%%.*}"
  if [[ "${libavformat_major}" -lt 60 ]]; then
    echo "" >&2
    echo "Warning: this system packages libavformat ${libavformat_version}" >&2
    echo "(FFmpeg $(( libavformat_major - 54 ))). qsv2flv needs FFmpeg 6.0 or" >&2
    echo "newer; see docs/Install.md." >&2
  fi
fi

echo "Ubuntu dependencies installed. Build with: ./build.sh"
