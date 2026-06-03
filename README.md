# Blockchain (C++23)

A **local, deterministic, testable blockchain** implementation in modern C++23. The project is built for engineering validation: correctness, security, reproducibility, and maintainability come before hype.

This is **not** a mainnet coin. There are no wallets, tokenomics, exchanges, or monetary claims. The goal is a small, robust system you can simulate, test under sanitizers, fuzz, restart safely, and eventually run as a **controlled public testnet** for protocol research.

---

## What it does today

| Area | Capabilities |
|------|----------------|
| **Simulator** | Single-process node: mine blocks, admit transactions, scripted multi-step scenarios, persist/restore ledger |
| **Local network** | Multi-process TCP relay: block sync, transaction announce, seed + client(s), disconnect/reconnect |
| **Consensus** | UTXO model, coinbase rules, maturity, deterministic chain replay, block connection |
| **Mempool** | Policy vs consensus separation, fee-rate ordering, capacity limits, deterministic eviction |
| **Block production** | Fee-rate block templates with self-validation before publish |
| **Storage** | Atomic `ledger.bin` (v2) with chain + mempool snapshot, checksum, format versioning |
| **P2P** | Framed messages (handshake, ping/pong, tx/block relay, reject), size limits, checksums |
| **Security posture** | Hostile-input validation, adversarial tests, fuzz targets, sanitizer-clean CI |

---

## Tech stack

| Layer | Choices |
|-------|---------|
| Language | **C++23** (`std::expected`, modern idioms) |
| Build | **CMake 3.25+**, **Ninja**, **CMake presets** |
| Testing | **CTest** (26 test executables), custom lightweight test runner |
| Static analysis | **clang-tidy**, **clang-format** |
| Sanitizers | **ASan + UBSan** (together), **TSan** (separate builds) |
| Fuzzing | **libFuzzer** (Clang): transactions, blocks, P2P, ledger, mempool |
| CI | **GitHub Actions** (PR + nightly + fuzz smoke) |
| Networking | TCP sockets (cross-platform; Linux, Windows) |
| Crypto | SHA-256 / double-SHA-256 (Bitcoin-style txids) |

**Supported toolchains (CI):** GCC 14, Clang 18 (tidy/fuzz).

---

## Architecture

High-level data flow: **network bytes → parse → validate → mempool/chain → storage**. Consensus logic is kept separate from networking, CLI, and persistence.

```text
                    ┌─────────────────────────────────────────┐
                    │           blockchain_node (CLI)          │
                    │  simulator │ relay server │ relay client │
                    └─────────────┬───────────────────────────┘
                                  │
         ┌────────────────────────┼────────────────────────┐
         ▼                        ▼                        ▼
   ┌───────────┐          ┌─────────────┐         ┌────────────┐
   │    net/   │          │   node/     │         │  storage/  │
   │ P2P wire  │◄────────►│ PeerState   │────────►│ ChainStore │
   │ TCP I/O   │          │ Simulator   │         │ ledger.bin │
   └───────────┘          └──────┬──────┘         └────────────┘
                                 │
              ┌──────────────────┼──────────────────┐
              ▼                  ▼                  ▼
        ┌───────────┐     ┌─────────────┐    ┌──────────────┐
        │ validation│     │  mempool/   │    │ production/  │
        │ tx/block  │     │  policy     │    │ block_builder│
        └─────┬─────┘     └──────┬──────┘    └──────┬───────┘
              │                  │                   │
              └────────────┬─────┴───────────────────┘
                           ▼
                    ┌─────────────┐
                    │ consensus/  │
                    │ Chain UTXO  │
                    └─────────────┘
                           ▲
                    ┌──────┴──────┐
                    │  protocol/  │
                    │  crypto/    │
                    │ serialization│
                    └─────────────┘
```

### Module responsibilities

| Module | Role |
|--------|------|
| `protocol/` | Blocks, transactions, headers, documented constants |
| `serialization/` | Canonical byte I/O, deterministic encoding |
| `crypto/` | SHA-256, hash utilities |
| `validation/` | Consensus rules for transactions and blocks |
| `state/` | UTXO set |
| `consensus/` | Chain creation, block submission, tip tracking |
| `mempool/` | Admission policy, fee-rate ordering, eviction, restore |
| `production/` | Deterministic block template builder |
| `storage/` | Ledger encode/decode, atomic writes, replay on load |
| `net/` | P2P message envelope, payloads, TCP helpers |
| `node/` | Config, genesis, simulator, peer state, network relay |
| `cli/` | `blockchain_node` executable |

### Design principles

- **Determinism** — Same inputs produce the same mempool ordering and block templates.
- **Layered validation** — Parse → envelope → semantics → apply to state.
- **Policy vs consensus** — A tx can be consensus-valid but rejected by mempool policy (e.g. min relay feerate).
- **No silent corruption** — Invalid or truncated storage/messages fail with explicit error codes.
- **No hardcoding** — Ports, peers, paths, and limits come from CLI flags or JSON config (protocol constants are named and documented).

---

## Repository layout

```text
.
├── CMakeLists.txt          # Top-level build
├── CMakePresets.json       # debug, release, asan-ubsan, tsan, fuzz
├── include/blockchain/     # Public headers (mirrors src/)
├── src/                    # Implementation
├── tests/                  # Unit & integration tests
├── fuzz/                   # libFuzzer targets
├── benchmarks/             # Hot-path benchmarks
├── configs/                # Example JSON configs
├── scripts/                # ci.sh, format.sh, local_network.sh, …
└── .github/workflows/      # CI, nightly, fuzz
```

---

## Quick start

### Prerequisites

- CMake ≥ 3.25  
- Ninja  
- C++23 compiler (GCC 14+ or Clang 18+ recommended)

On Ubuntu:

```bash
sudo apt-get install cmake ninja-build g++-14
```

### Build & test

```bash
cmake --preset debug
cmake --build --preset debug --parallel
ctest --preset debug --output-on-failure
```

Or use the local CI script:

```bash
./scripts/ci.sh
PRESET=asan-ubsan ./scripts/ci.sh
PRESET=tsan       ./scripts/ci.sh
```

Binary output: `build/debug/src/blockchain_node` (path varies by preset).

### Run the simulator

Mine three empty blocks:

```bash
./build/debug/src/blockchain_node \
  --genesis-timestamp 1700000000 \
  --mine-blocks 3 \
  --block-subsidy 5000 \
  --coinbase-maturity 1
```

Or use an example config:

```bash
./build/debug/src/blockchain_node --config configs/simulator.example.json
```

Persist and restore:

```bash
./build/debug/src/blockchain_node --config configs/simulator.example.json \
  --data-dir ./data/run1 --persist --mine-blocks 2

./build/debug/src/blockchain_node --config configs/simulator.example.json \
  --data-dir ./data/run1 --restore
```

### Run a local two-node network

Starts a relay seed (mines blocks) and a client that syncs over TCP:

```bash
./scripts/local_network.sh
```

Two clients against one seed:

```bash
SCENARIO=two-client ./scripts/local_network.sh
```

Manual relay example:

```bash
# Terminal 1 — seed
./build/debug/src/blockchain_node \
  --network-mode relay \
  --mine-blocks 2 \
  --listen-port 0 \
  --port-file ./seed.port \
  --data-dir ./data/seed

# Terminal 2 — client (use port from seed.port)
./build/debug/src/blockchain_node \
  --network-mode relay \
  --peer 127.0.0.1:<PORT> \
  --persist \
  --data-dir ./data/client
```

See `configs/relay_seed.example.json` and `configs/relay_client.example.json`.

---

## Configuration

Configuration is merged from **JSON file** (`--config`) and **CLI flags** (flags override file). All values are validated at startup.

Common flags:

| Flag | Description |
|------|-------------|
| `--data-dir` | Ledger and node data directory |
| `--persist` / `--restore` | Save or load `ledger.bin` |
| `--mine-blocks` | Mine N blocks after genesis |
| `--network-mode` | `ping` or `relay` |
| `--listen-port` | TCP server (0 = ephemeral) |
| `--peer` | Client connect target `host:port` |
| `--mempool-max-transactions` | Mempool tx count limit |
| `--mempool-max-bytes` | Mempool byte limit |
| `--min-relay-feerate` | Minimum fee per serialized byte |
| `--announce-tx-file` | Relay client: announce tx after sync |
| `--relay-max-sessions` | Relay server: sequential peer sessions |

Run `blockchain_node --help` for the full list.

---

## Persistent storage

Ledger file: `<data_dir>/ledger.bin` (written atomically via `ledger.bin.tmp`).

**Format v2** stores:

- Consensus parameters (subsidy, maturity)
- Full block chain (genesis first)
- Mempool snapshot (canonical tx order)

On restore, every block is replayed through validation and every mempool tx is re-admitted. Corrupt, truncated, or checksum-failed files are rejected — the node does not silently continue from bad state.

---

## P2P protocol (local network)

Messages use a fixed envelope: magic, version, type, payload length, payload, checksum. Supported types include handshake, ping/pong, transaction announce, block request/response, and reject.

All network input is treated as **untrusted**: oversize frames, bad checksums, unknown types, invalid payloads, and mid-stream disconnects are covered by unit, integration, and adversarial tests.

---

## Testing & quality

| Category | Examples |
|----------|----------|
| Unit | Serialization, validation, mempool, block builder, config |
| Integration | Simulator persist/restore, relay sync, multi-process spawn |
| Adversarial | Malformed P2P, corrupted ledger, invalid mempool on disk |
| Fuzz | Transaction/block/P2P/ledger/mempool parsers |

**CI (every PR):** debug + release + ASan/UBSan + TSan builds, tests, clang-format, clang-tidy.

**Nightly:** adversarial suite, benchmarks, extended scenarios.

**Fuzz (PR smoke + schedule):** 30-second libFuzzer runs per target.

---

## Benchmarks

```bash
./scripts/benchmark.sh
```

Covers serialization, hashing, validation, and other hot paths. Results can be stored as CI artifacts on nightly runs.

---

## What is intentionally out of scope (for now)

- Signatures / script authorization (types are reserved for later stages)
- Chain reorgs and dependency chains inside the mempool
- Public internet deployment or mainnet branding
- Wallets, mining pools, token economics

---

## Contributing

1. Match existing style (`./scripts/format.sh`).
2. Keep consensus paths deterministic and tested.
3. Add or update tests for behavior changes; run `./scripts/ci.sh` before opening a PR.
4. Do not hardcode network ports, peers, or secrets — use config.

---

## License

See repository license file if present. If no license is specified yet, treat the codebase as private/unlicensed until one is added.

---

## Disclaimer

This software is an **engineering and research** project. It has not been audited for production use. Do not use it to hold or transfer real value.
