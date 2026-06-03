#!/usr/bin/env bash
# Local reproduction of the CI build+test pipeline.
#
# Usage:
#   ./scripts/ci.sh
#   PRESET=asan-ubsan ./scripts/ci.sh
#   PRESET=tsan       ./scripts/ci.sh
#   PRESET=release    ./scripts/ci.sh
set -euo pipefail

PRESET="${PRESET:-debug}"

cmake --preset "${PRESET}"
cmake --build --preset "${PRESET}" --parallel
ctest --preset "${PRESET}" --output-on-failure
