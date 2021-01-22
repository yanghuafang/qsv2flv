#!/bin/bash

# llvm-env.sh — resolve the LLVM tools that format.sh and tidy.sh run.
#
# Sourced, never executed. Separate from build-env.sh, which also insists on
# FFmpeg and Qt: formatting should not require a media library.
#
# macOS takes both tools from Homebrew's llvm keg. Xcode's clang-format is not
# used even when present: its version tracks whatever Xcode is installed, so it
# is a property of the machine rather than of the repository. Unversioned, for
# the same reason the apt packages are — a pin comes due when the formula is
# retired, and .clang-tidy names every check it wants, so a different analyser
# cannot introduce an unlisted one. Linux takes them from PATH.
#
# tidy.sh enforces the one hard floor (clang-tidy 19+ on macOS).
#
# Overridable:
#   QSV2FLV_LLVM_VERSION   pin to a keg version on macOS, e.g. 20 for llvm@20
#   QSV2FLV_LLVM_PREFIX    an LLVM install to use instead; its bin/ is searched
#
# No `set -euo pipefail`: shell options are not scoped to a sourced file.

# Echoes an absolute path to the tool, or nothing.
qsv2flv_find_llvm_tool() {
  qsv2flv_tool_name="$1"

  if [[ -n "${QSV2FLV_LLVM_PREFIX:-}" ]]; then
    if [[ -x "${QSV2FLV_LLVM_PREFIX}/bin/${qsv2flv_tool_name}" ]]; then
      echo "${QSV2FLV_LLVM_PREFIX}/bin/${qsv2flv_tool_name}"
    fi
    return 0
  fi

  if [[ "$(uname -s)" == Darwin ]]; then
    command -v brew >/dev/null 2>&1 || return 0
    # `brew --prefix llvm` resolves only the unversioned formula, so a machine
    # with just llvm@20 needs it asked for by full name.
    for qsv2flv_formula in \
        ${QSV2FLV_LLVM_VERSION:+"llvm@${QSV2FLV_LLVM_VERSION}"} \
        llvm \
        llvm@22 llvm@21 llvm@20 llvm@19; do
      qsv2flv_keg="$(brew --prefix "${qsv2flv_formula}" 2>/dev/null || true)"
      if [[ -n "${qsv2flv_keg}" && -x "${qsv2flv_keg}/bin/${qsv2flv_tool_name}" ]]; then
        echo "${qsv2flv_keg}/bin/${qsv2flv_tool_name}"
        return 0
      fi
    done
    return 0
  fi

  # Linux: apt puts both on PATH.
  command -v "${qsv2flv_tool_name}" 2>/dev/null || true
}

# Here so format.sh and tidy.sh give the same install advice.
qsv2flv_llvm_missing() {
  qsv2flv_tool_name="$1"
  echo "${qsv2flv_tool_name} not found." >&2
  if [[ "$(uname -s)" == Darwin ]]; then
    echo "  This project takes the LLVM tools on macOS from Homebrew and does" >&2
    echo "  not use the copies that ship with Xcode. Install the keg:" >&2
    echo "" >&2
    echo "    brew install llvm" >&2
    echo "" >&2
    echo "  or ./install-deps-macos.sh, which does it for you. To pin a version," >&2
    echo "  set QSV2FLV_LLVM_VERSION=20; to point at an install elsewhere, set" >&2
    echo "  QSV2FLV_LLVM_PREFIX to its root." >&2
  else
    echo "  sudo apt install clang-format clang-tidy" >&2
    echo "  or ./install-deps-ubuntu.sh" >&2
  fi
}
