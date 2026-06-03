#ifndef BLOCKCHAIN_NODE_GENESIS_HPP
#define BLOCKCHAIN_NODE_GENESIS_HPP

#include "blockchain/node/config.hpp"
#include "blockchain/protocol/block.hpp"

namespace blockchain::node {

// Builds the deterministic genesis block for the simulator. Given the same
// config inputs, this always produces an identical block (and therefore an
// identical block hash). The genesis block has no transactions at Stage 1.
[[nodiscard]] protocol::Block build_genesis_block(const NodeConfig& config);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_GENESIS_HPP
