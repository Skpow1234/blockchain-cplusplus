#!/usr/bin/env bash
# Run clang-tidy against the compilation database produced by a configured preset.
#
# Usage:
#   ./scripts/tidy.sh            # uses build/debug
#   BUILD_DIR=build/release ./scripts/tidy.sh
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build/debug}"

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "No compile_commands.json in ${BUILD_DIR}." >&2
  echo "Run 'cmake --preset debug' first." >&2
  exit 1
fi

run-clang-tidy -p "${BUILD_DIR}" -warnings-as-errors='*' \
  "$(pwd)/src" "$(pwd)/include"
