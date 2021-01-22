#!/bin/bash

# run-tests.sh — run the suites through CTest, from a CMake preset.
#
# A thin wrapper: the test settings that used to be spelled out here (always
# --output-on-failure) live in the matching testPreset, so `ctest --preset NAME`
# on its own does the same thing. This adds the environment check and the
# "build first" message, which CTest cannot give.
#
# Every fixture is built by the suite itself, so this needs no sample video and
# no network; see docs/Testing.md for how that works.
#
# Usage:
#   ./run-tests.sh                            all suites, release preset
#   ./run-tests.sh ReaderTest                 one suite, by name or regex
#   ./run-tests.sh --preset ci-asan-ubsan     the sanitizer build's suites

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=build-env.sh
source "${script_dir}/build-env.sh" || exit 1

preset="${QSV2FLV_DEFAULT_PRESET}"
filter=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      if [[ $# -lt 2 ]]; then
        echo "--preset needs a name; run cmake --list-presets." >&2
        exit 1
      fi
      preset="$2"
      shift 2
      ;;
    --preset=*) preset="${1#--preset=}"; shift ;;
    -h|--help) echo "Usage: $0 [--preset NAME] [TEST_REGEX]" >&2; exit 0 ;;
    *) filter="$1"; shift ;;
  esac
done

build_dir="$(qsv2flv_build_dir "${preset}")"
if [[ ! -f "${build_dir}/CTestTestfile.cmake" ]]; then
  echo "No tests configured in ${build_dir}." >&2
  echo "Run ./build.sh --preset ${preset} first." >&2
  exit 1
fi

# From the repo root: `ctest --preset` reads CMakePresets.json from the working
# directory, not from the build tree it is about to run.
cd "${QSV2FLV_REPO_DIR}"

if [[ -n "${filter}" ]]; then
  ctest --preset "${preset}" -R "${filter}"
else
  ctest --preset "${preset}"
fi
