#!/usr/bin/env bash
# Build and run the test suite under sanitizers.
#
# Usage:
#   ./scripts/sanitize.sh             # ASan + UBSan
#   MODE=tsan ./scripts/sanitize.sh   # ThreadSanitizer
set -euo pipefail

cd "$(dirname "$0")/.."

MODE="${MODE:-asan-ubsan}"

case "${MODE}" in
  asan-ubsan|tsan) ;;
  *) echo "Unknown MODE='${MODE}' (expected 'asan-ubsan' or 'tsan')" >&2; exit 1 ;;
esac

export ASAN_OPTIONS="${ASAN_OPTIONS:-abort_on_error=1:detect_leaks=1:strict_string_checks=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
export TSAN_OPTIONS="${TSAN_OPTIONS:-halt_on_error=1}"

cmake --preset "${MODE}"
cmake --build --preset "${MODE}" --parallel
ctest --preset "${MODE}" --output-on-failure
