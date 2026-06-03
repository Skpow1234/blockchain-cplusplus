#ifndef BLOCKCHAIN_PRODUCTION_BLOCK_BUILDER_HPP
#define BLOCKCHAIN_PRODUCTION_BLOCK_BUILDER_HPP

#include <cstddef>
#include <cstdint>

#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/state/utxo_set.hpp"

// Deterministic block production for the simulator. Given the same tip,
// timestamp, mempool contents, UTXO set, and parameters, this always produces
// the identical block. Block production is kept separate from validation,
// networking, and storage.
namespace blockchain::production {

// Caller-supplied production parameters (from configuration; not hardcoded).
struct BlockTemplateParams {
  std::uint32_t version = 0;
  std::uint32_t max_block_size_bytes = 0;
  std::uint32_t max_transactions = 0;
};

struct BlockTemplate {
  protocol::Block block;
  std::uint64_t total_fees = 0;
  std::size_t selected_count = 0;
};

// Builds a block on top of `tip`, selecting transactions from `mempool` in
// fee-rate order (highest first) while respecting the size and count budgets,
// and validating each against `utxos` (so the candidate is internally
// consistent). The resulting block links to the tip (prev = tip.hash(),
// height = tip.height + 1) and carries the correct Merkle root.
//
// Invariant enforced here: the produced block always passes connect_block
// against `utxos`. If that ever fails, an error is returned rather than
// emitting a block the local validator would reject.
[[nodiscard]] Result<BlockTemplate> build_block_template(const protocol::BlockHeader& tip,
                                                         std::uint64_t timestamp,
                                                         const mempool::Mempool& mempool,
                                                         const state::UtxoSet& utxos,
                                                         const BlockTemplateParams& params);

}  // namespace blockchain::production

#endif  // BLOCKCHAIN_PRODUCTION_BLOCK_BUILDER_HPP
