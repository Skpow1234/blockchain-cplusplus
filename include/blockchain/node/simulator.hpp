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
};

// Runs the deterministic single-process simulator: initialize the chain from
// genesis, optionally mine `config.mine_blocks` empty (or mempool-filled)
// blocks, and return the resulting tip state. No networking or storage.
[[nodiscard]] Result<SimulatorSummary> run_simulator(const NodeConfig& config);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_SIMULATOR_HPP
