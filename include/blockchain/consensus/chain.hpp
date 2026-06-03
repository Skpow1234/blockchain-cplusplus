#ifndef BLOCKCHAIN_CONSENSUS_CHAIN_HPP
#define BLOCKCHAIN_CONSENSUS_CHAIN_HPP

#include <cstdint>
#include <utility>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/state/utxo_set.hpp"

// The active chain: the single source of truth for the current tip header,
// height, and confirmed UTXO set. It ties the validation layer to a growing
// chain by enforcing the contextual header-linking rules that connect_block
// (which is intentionally context-free about the *previous* block) does not.
//
// What it owns:    the tip header and the confirmed UTXO set.
// What it does NOT own: the mempool, networking, storage, or block production.
//                       (submit_block may *prune* a caller-owned mempool, but it
//                       never takes ownership of it.)
//
// Invariants:
//   * tip() is always a header that has been fully validated and connected.
//   * utxos() reflects the cumulative state transition of every connected block
//     from genesis up to and including tip().
//   * submit_block is all-or-nothing: on any failure neither the tip, the UTXO
//     set, nor the mempool is modified.
//
// Determinism: no wall-clock, randomness, or environment input is consulted.
// Re-running the same genesis + block sequence yields an identical tip and UTXO
// set. Reorgs/disconnect are a planned follow-up (they require undo data).
namespace blockchain::consensus {

class Chain {
 public:
  // Initializes a chain from its genesis block. The genesis block must:
  //   * have height 0,
  //   * reference the zero hash as prev_block,
  //   * carry the supported block version,
  //   * pass check_block_sanity, and
  //   * connect cleanly to an empty UTXO set.
  // Returns kInvalidBlock / kUnsupportedVersion on violation.
  [[nodiscard]] static Result<Chain> create(const protocol::Block& genesis);

  [[nodiscard]] const protocol::BlockHeader& tip() const noexcept { return tip_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return tip_.height; }
  [[nodiscard]] crypto::Hash256 tip_hash() const { return tip_.hash(); }
  [[nodiscard]] const state::UtxoSet& utxos() const noexcept { return utxos_; }

  // Connects `block` on top of the current tip. Performs, in order:
  //   1. contextual header-linking checks:
  //        * version == supported block version        -> kUnsupportedVersion
  //        * prev_block == tip().hash()                 -> kInvalidBlock
  //        * height == tip().height + 1 (no overflow)   -> kInvalidBlock
  //        * timestamp >= tip().timestamp (monotonic)   -> kInvalidBlock
  //   2. context-free sanity checks (check_block_sanity)
  //   3. the full UTXO state transition (connect_block)
  //
  // On success the tip and UTXO set advance, every included transaction is
  // removed from `mempool` (when non-null), and the total collected fees are
  // returned. On failure nothing is modified and an inspectable error is
  // returned.
  [[nodiscard]] Result<std::uint64_t> submit_block(const protocol::Block& block,
                                                   mempool::Mempool* mempool = nullptr);

 private:
  Chain(protocol::BlockHeader tip, state::UtxoSet utxos)
      : tip_(tip), utxos_(std::move(utxos)) {}

  protocol::BlockHeader tip_{};
  state::UtxoSet utxos_;
};

}  // namespace blockchain::consensus

#endif  // BLOCKCHAIN_CONSENSUS_CHAIN_HPP
