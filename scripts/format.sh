#!/bin/bash

# format.sh — apply the repo's source formatting rules in place.
#
# Two passes over the same file set:
#   1. clang-format, using the .clang-format at the repo root (Google style,
#      2-space indent, 80 columns).
#   2. strip trailing whitespace, which clang-format leaves inside block
#      comments.
#
# Modes:
#   (default)   rewrite files in place
#   --check     report what would change and exit 1 without writing; for CI

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
# shellcheck source=llvm-env.sh
source "${script_dir}/llvm-env.sh" || exit 1

check_only=false
while [[ $# -gt 0 ]]; do
  case "$1" in
    --check) check_only=true; shift ;;
    -h|--help) echo "Usage: $0 [--check]" >&2; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      echo "Usage: $0 [--check]" >&2
      exit 1
      ;;
  esac
done

clang_format="$(qsv2flv_find_llvm_tool clang-format)"
if [[ -z "${clang_format}" ]]; then
  qsv2flv_llvm_missing clang-format
  exit 1
fi

# Print the binary and version: a --check failure on one machine and not
# another is usually version skew rather than a real slip, and naming the
# binary turns that from a puzzle into a one-line diagnosis.
echo "Using ${clang_format} — $("${clang_format}" --version)"

# GNU sed takes -i; BSD sed (macOS) requires an explicit empty suffix.
if sed --version >/dev/null 2>&1; then
  sed_inplace=(sed -i)
else
  sed_inplace=(sed -i '')
fi

list_sources() {
  find "${repo_root}/src" "${repo_root}/tests" \
    -type f \( -name '*.cc' -o -name '*.h' \) -print
}

status=0

if [[ "$check_only" == true ]]; then
  while IFS= read -r file; do
    if ! "${clang_format}" "$file" | diff -q - "$file" >/dev/null 2>&1; then
      echo "needs clang-format: ${file#"${repo_root}"/}"
      status=1
    fi
    if grep -qE '[[:blank:]]+$' "$file"; then
      echo "trailing whitespace: ${file#"${repo_root}"/}"
      status=1
    fi
  done < <(list_sources | sort)

  if [[ "$status" -eq 0 ]]; then
    echo "All files are formatted."
  fi
  exit "$status"
fi

formatted=0
stripped=0
while IFS= read -r file; do
  "${clang_format}" -i "$file"
  formatted=$((formatted + 1))
  # After clang-format, so this pass has the final say.
  if grep -qE '[[:blank:]]+$' "$file"; then
    "${sed_inplace[@]}" -E 's/[[:blank:]]+$//' "$file"
    stripped=$((stripped + 1))
  fi
done < <(list_sources | sort)

echo "clang-format applied to ${formatted} file(s); trailing whitespace stripped from ${stripped}."
