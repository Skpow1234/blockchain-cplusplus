#!/usr/bin/env bash
# Build and run benchmarks in Release mode. Results are written to
# benchmark_results/ (git-ignored) for later comparison.
#
# Usage:
#   ./scripts/benchmark.sh
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="build/release-bench"
OUT_DIR="benchmark_results"
mkdir -p "${OUT_DIR}"

cmake -S . -B "${BUILD_DIR}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBLOCKCHAIN_BUILD_BENCHMARKS=ON \
  -DBLOCKCHAIN_BUILD_TESTS=OFF
cmake --build "${BUILD_DIR}" --parallel

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
"${BUILD_DIR}/benchmarks/blockchain_benchmarks" | tee "${OUT_DIR}/bench-${stamp}.txt"
