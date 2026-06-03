#ifndef BLOCKCHAIN_NODE_SIMULATOR_HPP
#define BLOCKCHAIN_NODE_SIMULATOR_HPP

#include <cstdint>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/protocol/transaction.hpp"

namespace blockchain::node {

// Summary of a completed Stage-1 simulator run. Suitable for logging and tests.
struct SimulatorSummary {
  crypto::Hash256 genesis_hash{};
  crypto::Hash256 tip_hash{};
  std::uint32_t height = 0;
  std::size_t utxo_count = 0;
  // Blocks mined during this run (excludes blocks loaded via --restore).
  std::uint32_t blocks_mined = 0;
  std::uint32_t txs_admitted = 0;
  std::uint32_t txs_included = 0;
  std::uint64_t total_fees = 0;
  bool restored_from_disk = false;
};

// One deterministic step: admit transactions, then mine blocks from the mempool.
struct SimulatorStep {
  std::vector<protocol::Transaction> submit_txs;
  std::uint32_t mine_blocks = 0;
};

// Optional scenario controls for integration tests and scripted runs. When
// `steps` is empty, the simulator mines `config.mine_blocks` empty blocks.
struct SimulatorOptions {
  std::vector<SimulatorStep> steps;
  // Zero fields select documented Stage-1 simulator defaults.
  mempool::MempoolLimits mempool_limits{};
};

// Runs the deterministic single-process simulator: initialize the chain from
// genesis (or restore from disk), optionally admit transactions and mine blocks,
// and optionally persist the ledger under `config.data_dir`.
[[nodiscard]] Result<SimulatorSummary> run_simulator(const NodeConfig& config,
                                                     const SimulatorOptions& options = {});

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_SIMULATOR_HPP
