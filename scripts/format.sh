#!/usr/bin/env bash
# Format (or check formatting of) all C++ sources with clang-format.
#
# Usage:
#   ./scripts/format.sh          # rewrite files in place
#   ./scripts/format.sh --check  # fail if any file is not formatted
set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
MODE="${1:-}"

mapfile -t files < <(find src include tests fuzz benchmarks \
  \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null || true)

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No source files found."
  exit 0
fi

if [[ "${MODE}" == "--check" ]]; then
  "${CLANG_FORMAT}" --dry-run --Werror "${files[@]}"
  echo "Formatting OK."
else
  "${CLANG_FORMAT}" -i "${files[@]}"
  echo "Formatted ${#files[@]} files."
fi
