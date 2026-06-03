#include "blockchain/node/genesis.hpp"

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"

namespace blockchain::node {

protocol::Block build_genesis_block(const NodeConfig& config) {
  protocol::Block genesis;
  genesis.header.version = protocol::kBlockVersion;
  genesis.header.prev_block = crypto::zero_hash();
  genesis.header.timestamp = config.genesis_timestamp;
  genesis.header.height = 0;
  // No transactions at Stage 1; the Merkle root of an empty set is the zero
  // hash by definition (see compute_merkle_root).
  genesis.header.merkle_root = protocol::compute_merkle_root(genesis.transactions);
  return genesis;
}

}  // namespace blockchain::node
