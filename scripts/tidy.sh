#!/bin/bash

# tidy.sh — run clang-tidy against the curated check list in .clang-tidy.
#
# Needs the compile database CMake writes (CMAKE_EXPORT_COMPILE_COMMANDS), so
# build.sh has to have run at least once. Only src/ is analysed: tests/ is
# fixtures, and the Qt moc output that lands in the build tree is generated.
#
# Usage:
#   ./tidy.sh                     report findings, exit 1 if there are any
#   ./tidy.sh --fix               apply what clang-tidy can fix, then the rest
#   ./tidy.sh --preset ci-release the compile database CI analyses

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=build-env.sh
source "${script_dir}/build-env.sh" || exit 1
# shellcheck source=llvm-env.sh
source "${script_dir}/llvm-env.sh" || exit 1

# Held as a string rather than an array. macOS ships Bash 3.2, where expanding
# an empty array under `set -u` -- "${fix_args[@]}" with nothing in it -- is an
# unbound-variable error rather than the empty list every later Bash produces.
# Unquoted word splitting on a value this script sets itself is safe, and it
# behaves the same on 3.2 and 5.x.
fix_args=""
preset="${QSV2FLV_DEFAULT_PRESET}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --fix) fix_args="--fix --fix-errors"; shift ;;
    --preset)
      if [[ $# -lt 2 ]]; then
        echo "--preset needs a name; run cmake --list-presets." >&2
        exit 1
      fi
      preset="$2"
      shift 2
      ;;
    --preset=*) preset="${1#--preset=}"; shift ;;
    -h|--help) echo "Usage: $0 [--fix] [--preset NAME]" >&2; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
  esac
done

clang_tidy="$(qsv2flv_find_llvm_tool clang-tidy)"
if [[ -z "${clang_tidy}" ]]; then
  qsv2flv_llvm_missing clang-tidy
  exit 1
fi

# The preset has to be one that builds the GUI: this analyses all of src/,
# which includes src/gui, and a file missing from the compile database is
# analysed with default flags and fails on its first Qt include.
build_dir="$(qsv2flv_build_dir "${preset}")"
database="${build_dir}/compile_commands.json"
if [[ ! -f "${database}" ]]; then
  echo "No compile database at ${database}." >&2
  echo "Run ./build.sh --preset ${preset} first." >&2
  exit 1
fi

# On macOS, hand clang-tidy the SDK path explicitly. CMake omits -isysroot from
# compile_commands.json because the compiler it recorded is Apple's, which finds
# the SDK on its own; any other clang-tidy -- Homebrew's, or a pip wheel -- does
# not, and reports `'cstddef' file not found` on the first header of every file.
# That reads like a broken checkout rather than a missing flag, so supply it.
#
# Held as a string for the same Bash 3.2 reason as fix_args above.
extra_args=""
if [[ "$(uname -s)" == Darwin ]] && command -v xcrun >/dev/null 2>&1; then
  sdk_path="$(xcrun --show-sdk-path 2>/dev/null || true)"
  if [[ -n "${sdk_path}" ]]; then
    extra_args="--extra-arg=-isysroot --extra-arg=${sdk_path}"
  fi
fi

clang_tidy_version="$("${clang_tidy}" --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -1)"
echo "Using ${clang_tidy} — $("${clang_tidy}" --version | grep -i version | head -1)"

# macOS needs clang-tidy 19 or newer, and the reason has nothing to do with the
# checks: the SDK's libc++ uses __builtin_clzg, which Clang gained in 19. An
# older analyser fails to parse <algorithm> and reports a wall of errors from
# inside the standard library, which looks like a broken checkout rather than a
# tool that is too old. Say so instead.
if [[ "$(uname -s)" == Darwin && -n "${clang_tidy_version}" ]] \
   && [[ "${clang_tidy_version}" -lt 19 ]]; then
  echo "clang-tidy ${clang_tidy_version} cannot parse the macOS SDK: libc++" >&2
  echo "needs __builtin_clzg, a Clang 19 builtin. Install a newer one:" >&2
  echo "  brew install llvm" >&2
  exit 1
fi

# One invocation per file rather than run-clang-tidy, which is not packaged on
# every platform this targets. The tree is small enough that the difference is
# seconds.
status=0
while IFS= read -r file; do
  echo "  ${file#"${QSV2FLV_REPO_DIR}"/}"
  # shellcheck disable=SC2086  # deliberate word splitting; see fix_args above
  if ! "${clang_tidy}" -p "${build_dir}" ${extra_args} ${fix_args} \
       "${file}" --quiet 2>&1 | grep -v '^$'; then
    : # grep found nothing to print, which is the clean case
  fi
  # clang-tidy exits non-zero when it emits a diagnostic, and the pipe above
  # hides that, so ask it again for the status alone on a clean pass.
  # shellcheck disable=SC2086  # as above
  if ! "${clang_tidy}" -p "${build_dir}" ${extra_args} "${file}" \
       --quiet >/dev/null 2>&1; then
    status=1
  fi
done < <(find "${QSV2FLV_REPO_DIR}/src" -type f -name '*.cc' | sort)

if [[ "${status}" -eq 0 ]]; then
  echo "clang-tidy: clean."
else
  echo "clang-tidy: findings above." >&2
fi
exit "${status}"
