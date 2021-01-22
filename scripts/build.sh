#!/bin/bash

# build.sh — configure and build qsv2flv from a CMake preset.
#
# Every build setting lives in CMakePresets.json; this script supplies the
# environment those presets are configured in (see build-env.sh) and nothing
# else. That split is deliberate: a build configuration named in a shell script
# has to be reverse-engineered from argument parsing, while `cmake
# --list-presets` enumerates the ones here and CI names them verbatim.
#
# Usage:
#   ./build.sh                          the release preset
#   ./build.sh --preset ci-asan-ubsan   exactly what the sanitizer job builds
#
# Override the job count with QSV2FLV_BUILD_JOBS=N, and the build location with
# a CMakeUserPresets.json that inherits the preset and sets binaryDir.

# Stop at the first failing step, on an unset variable, and on a failure
# anywhere in a pipe. Without -e a failed configure is followed by a build
# anyway, and the message the user ends on is make's complaint about re-running
# configure with CMake's real error scrolled off the top.
set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=build-env.sh
source "${script_dir}/build-env.sh" || exit 1

usage() {
  cat <<'EOF'
Usage: build.sh [--preset NAME] [--clean] [-h]

Configure and build qsv2flv from a CMake preset, into a sibling of the repo.

Options:
  --preset NAME  Which preset to build (default: release).
                 Run `cmake --list-presets` in the repo root for the full list.
  --clean        Delete this preset's build directory first.
  -h, --help     Show this help.

Environment:
  QSV2FLV_BUILD_JOBS  Parallel jobs (default: one per logical core).
EOF
}

preset="${QSV2FLV_DEFAULT_PRESET}"
clean=false

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
    --clean) clean=true; shift ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      echo "Build settings are preset names now, not flags; see --help." >&2
      usage >&2
      exit 1
      ;;
  esac
done

build_dir="$(qsv2flv_build_dir "${preset}")"

if [[ "$clean" == true ]]; then
  rm -rf "${build_dir}"
fi

# A package upgrade can leave the CMake cache pointing at a directory that no
# longer exists — pkg_check_modules caches its answer, so CMake fails on the
# stale path rather than asking again. Detect that and start over, since the
# error it produces otherwise names the path but not the fix.
cache_file="${build_dir}/CMakeCache.txt"
if [[ -f "${cache_file}" ]]; then
  stale_path=""
  while IFS= read -r cached_path; do
    if [[ -n "${cached_path}" && ! -e "${cached_path}" ]]; then
      stale_path="${cached_path}"
      break
    fi
  done < <(grep -oE '(-I|:FILEPATH=)/[^ ";]+' "${cache_file}" \
           | sed -E 's|^(-I\|:FILEPATH=)||' | sort -u)

  if [[ -n "${stale_path}" ]]; then
    echo "Cached dependency path is gone (${stale_path}); reconfiguring from scratch."
    rm -rf "${build_dir}"
  fi
fi

if [[ -n "${QSV2FLV_BUILD_JOBS:-}" ]]; then
  build_jobs="${QSV2FLV_BUILD_JOBS}"
elif command -v nproc >/dev/null 2>&1; then
  build_jobs="$(nproc)"                      # Linux
elif [[ "$(uname -s)" == Darwin ]]; then
  build_jobs="$(sysctl -n hw.logicalcpu)"    # macOS
else
  build_jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
fi

# From the repo root, because `cmake --preset` looks for CMakePresets.json in
# the working directory rather than beside the source it is about to configure.
cd "${QSV2FLV_REPO_DIR}"

echo "Building preset ${preset} with ${build_jobs} parallel jobs into ${build_dir}..."
cmake --preset "${preset}"
cmake --build --preset "${preset}" --parallel "${build_jobs}"
