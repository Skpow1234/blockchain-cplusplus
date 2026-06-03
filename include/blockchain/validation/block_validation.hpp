#ifndef BLOCKCHAIN_VALIDATION_BLOCK_VALIDATION_HPP
#define BLOCKCHAIN_VALIDATION_BLOCK_VALIDATION_HPP

#include <cstdint>

#include "blockchain/error.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/state/utxo_set.hpp"

// Consensus-level block validation and state transition. Deterministic and free
// of networking, storage, and configuration concerns.
namespace blockchain::validation {

// Context-free checks that require no chain state:
//   * serialized block size <= kMaxBlockSizeBytes
//   * transaction count <= kMaxTransactionsPerBlock
//   * header.merkle_root equals the computed Merkle root of the transactions
//   * no duplicate transactions (by txid) within the block
//   * every transaction passes check_transaction_sanity
//
// Returns kInvalidBlock for block-level problems, or the underlying
// transaction error for a malformed contained transaction.
[[nodiscard]] Result<void> check_block_sanity(const protocol::Block& block);

// Validates the block against `utxos` and, on success, applies the full state
// transition: each transaction's inputs are spent and its outputs become new
// coins (at the block's height). Transactions are processed in order, so a
// transaction may spend an output created by an earlier transaction in the same
// block, but not a later one.
//
// All-or-nothing: the work is performed on a copy and committed to `utxos` only
// if the entire block is valid (no partial application on failure). Returns the
// total fees collected (sum of per-transaction fees).
//
// Stage-1 notes: there is no coinbase/reward transaction yet, so a block's
// transactions must spend pre-existing UTXOs; collected fees are returned but
// not redistributed. Block *disconnect* (reorg) requires undo data and is a
// planned follow-up.
[[nodiscard]] Result<std::uint64_t> connect_block(const protocol::Block& block,
                                                  state::UtxoSet& utxos);

}  // namespace blockchain::validation

#endif  // BLOCKCHAIN_VALIDATION_BLOCK_VALIDATION_HPP
