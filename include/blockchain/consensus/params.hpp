#ifndef BLOCKCHAIN_CONSENSUS_PARAMS_HPP
#define BLOCKCHAIN_CONSENSUS_PARAMS_HPP

#include <cstdint>

#include "blockchain/protocol/constants.hpp"

namespace blockchain::consensus {

// Economic/consensus parameters that govern validation and block production.
// Carried explicitly (rather than read from globals) so that simulations and
// tests can choose deterministic values without changing protocol code. The
// defaults are the documented protocol constants.
struct ConsensusParams {
  // New coins minted by each block's coinbase (the coinbase may also claim the
  // block's transaction fees on top of this).
  std::uint64_t block_subsidy = protocol::kDefaultBlockSubsidy;

  // Number of blocks a coinbase output must age before it may be spent. A
  // coinbase created at height H is spendable only at height >= H + maturity.
  std::uint32_t coinbase_maturity = protocol::kDefaultCoinbaseMaturity;
};

}  // namespace blockchain::consensus

#endif  // BLOCKCHAIN_CONSENSUS_PARAMS_HPP
