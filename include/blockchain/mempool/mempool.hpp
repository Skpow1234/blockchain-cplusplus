#ifndef BLOCKCHAIN_MEMPOOL_MEMPOOL_HPP
#define BLOCKCHAIN_MEMPOOL_MEMPOOL_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"

namespace blockchain::mempool {

// Deterministic total order over 256-bit hashes (lexicographic over bytes as
// unsigned values). Keeps mempool indexing and tie-breaking reproducible.
struct Hash256Less {
  [[nodiscard]] bool operator()(const crypto::Hash256& lhs,
                                const crypto::Hash256& rhs) const noexcept {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(), [](std::byte a, std::byte b) {
          return std::to_integer<unsigned>(a) < std::to_integer<unsigned>(b);
        });
  }
};

// Resource limits for the mempool. Supplied explicitly by the caller (from
// configuration) rather than hardcoded.
struct MempoolLimits {
  std::uint32_t max_transactions = 0;
  std::uint64_t max_total_bytes = 0;
};

// A deterministic, policy-light transaction memory pool.
//
// Invariants:
//   * Every admitted transaction is consensus-valid against the UTXO set it was
//     accepted against (see validation::check_transaction_*).
//   * No two admitted transactions spend the same outpoint (no conflicts).
//   * A txid appears at most once.
//   * total_bytes() equals the summed serialized size of admitted entries.
//
// Stage-1 limitation: inputs must already exist in the *confirmed* UTXO set;
// chained spends of other in-mempool outputs are not yet supported (dependency
// tracking is a planned enhancement). The mempool owns admission only; it does
// not perform networking, storage, or block production.
class Mempool {
 public:
  struct Entry {
    protocol::Transaction transaction;
    crypto::Hash256 txid{};
    std::uint64_t fee = 0;
    std::uint64_t size_bytes = 0;
  };

  explicit Mempool(MempoolLimits limits) : limits_(limits) {}

  // Validates `tx` against `utxos` and admits it. Distinct, inspectable
  // rejection reasons:
  //   * kInvalidTransaction       - failed sanity/consensus checks, duplicate,
  //                                 or conflicts with an in-mempool transaction
  //   * kResourceLimitExceeded    - would exceed count or byte limits
  [[nodiscard]] Result<void> accept(const protocol::Transaction& tx, const state::UtxoSet& utxos);

  [[nodiscard]] bool contains(const crypto::Hash256& txid) const;
  [[nodiscard]] const Entry* find(const crypto::Hash256& txid) const;

  // Removes a transaction (e.g. after inclusion in a connected block). No-op if
  // absent. Frees the outpoints it had reserved for conflict detection.
  void remove(const crypto::Hash256& txid);

  [[nodiscard]] std::size_t size() const noexcept { return by_txid_.size(); }
  [[nodiscard]] bool empty() const noexcept { return by_txid_.empty(); }
  [[nodiscard]] std::uint64_t total_bytes() const noexcept { return total_bytes_; }

  // Deterministic ordering for block-template selection: highest fee-rate
  // (fee per byte) first, ties broken by ascending txid.
  [[nodiscard]] std::vector<Entry> entries_by_feerate() const;

 private:
  MempoolLimits limits_;
  std::map<crypto::Hash256, Entry, Hash256Less> by_txid_;
  std::map<protocol::OutPoint, crypto::Hash256, state::OutPointLess> spent_by_;
  std::uint64_t total_bytes_ = 0;
};

}  // namespace blockchain::mempool

#endif  // BLOCKCHAIN_MEMPOOL_MEMPOOL_HPP
