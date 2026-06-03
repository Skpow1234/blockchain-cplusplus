#!/usr/bin/env bash
# Local multi-process relay demo: seed node mines blocks, one or two clients sync over TCP.
# SCENARIO=default|two-client (default: single client sync)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PRESET="${PRESET:-debug}"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build/${PRESET}}"
BIN="${BIN:-${BUILD_DIR}/src/blockchain_node}"
SCENARIO="${SCENARIO:-default}"

if [[ ! -x "${BIN}" ]]; then
  echo "building blockchain_node (${PRESET})..." >&2
  cmake --preset "${PRESET}" -S "${ROOT}" -B "${BUILD_DIR}"
  cmake --build "${BUILD_DIR}" --target blockchain_node --parallel
fi

WORK="${ROOT}/.local_network_run"
PORT_FILE="${WORK}/seed.port"
SEED_DIR="${WORK}/seed"
CLIENT_DIR="${WORK}/client"
CLIENT2_DIR="${WORK}/client2"
mkdir -p "${WORK}"
rm -f "${PORT_FILE}"
rm -rf "${SEED_DIR}" "${CLIENT_DIR}" "${CLIENT2_DIR}"
mkdir -p "${SEED_DIR}" "${CLIENT_DIR}" "${CLIENT2_DIR}"

COMMON=(
  --genesis-timestamp 1700000100
  --block-subsidy 5000
  --coinbase-maturity 1
  --coinbase-recipient a100000000000000000000000000000000000000000000000000000000000000
)

SEED_EXTRA=(--mine-blocks 2)
if [[ "${SCENARIO}" == "two-client" ]]; then
  SEED_EXTRA+=(--relay-max-sessions 2)
fi

echo "starting relay seed (scenario=${SCENARIO})..."
"${BIN}" "${COMMON[@]}" \
  --network-mode relay \
  --data-dir "${SEED_DIR}" \
  "${SEED_EXTRA[@]}" \
  --listen-port 0 \
  --port-file "${PORT_FILE}" &
SEED_PID=$!

cleanup() {
  kill "${SEED_PID}" 2>/dev/null || true
  wait "${SEED_PID}" 2>/dev/null || true
}
trap cleanup EXIT

for _ in $(seq 1 200); do
  if [[ -s "${PORT_FILE}" ]]; then
    break
  fi
  sleep 0.05
done

PORT="$(tr -d '[:space:]' < "${PORT_FILE}")"
if [[ -z "${PORT}" || "${PORT}" == "0" ]]; then
  echo "seed did not publish a listen port" >&2
  exit 1
fi

echo "seed listening on 127.0.0.1:${PORT}"
echo "starting relay client..."
"${BIN}" "${COMMON[@]}" \
  --network-mode relay \
  --data-dir "${CLIENT_DIR}" \
  --persist \
  --peer "127.0.0.1:${PORT}"

echo "client synced; ledger at ${CLIENT_DIR}/ledger.bin"

if [[ "${SCENARIO}" == "two-client" ]]; then
  echo "starting second relay client..."
  "${BIN}" "${COMMON[@]}" \
    --network-mode relay \
    --data-dir "${CLIENT2_DIR}" \
    --persist \
    --peer "127.0.0.1:${PORT}"
  echo "second client synced; ledger at ${CLIENT2_DIR}/ledger.bin"
fi

wait "${SEED_PID}"
