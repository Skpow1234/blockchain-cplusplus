#ifndef BLOCKCHAIN_NODE_SIMULATOR_HPP
#define BLOCKCHAIN_NODE_SIMULATOR_HPP

#include <cstdint>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"

namespace blockchain::node {

// Summary of a completed Stage-1 simulator run. Suitable for logging and tests.
struct SimulatorSummary {
  crypto::Hash256 genesis_hash{};
  crypto::Hash256 tip_hash{};
  std::uint32_t height = 0;
  std::size_t utxo_count = 0;
  std::uint32_t blocks_mined = 0;
  bool restored_from_disk = false;
};

// Runs the deterministic single-process simulator: initialize the chain from
// genesis, optionally mine `config.mine_blocks`, optionally persist or restore
// the ledger under `config.data_dir` when persist/restore flags are set.
[[nodiscard]] Result<SimulatorSummary> run_simulator(const NodeConfig& config);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_SIMULATOR_HPP
