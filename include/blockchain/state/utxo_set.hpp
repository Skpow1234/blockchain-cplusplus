#ifndef BLOCKCHAIN_STATE_UTXO_SET_HPP
#define BLOCKCHAIN_STATE_UTXO_SET_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>

#include "blockchain/error.hpp"
#include "blockchain/protocol/transaction.hpp"

namespace blockchain::state {

// Deterministic ordering for OutPoints: compare txid bytes first (as unsigned
// values), then the output index. A total order is required so that the UTXO
// set iterates and serializes identically across runs and platforms.
struct OutPointLess {
  [[nodiscard]] bool operator()(const protocol::OutPoint& lhs,
                                const protocol::OutPoint& rhs) const noexcept {
    if (lhs.txid != rhs.txid) {
      return std::lexicographical_compare(
          lhs.txid.begin(), lhs.txid.end(), rhs.txid.begin(), rhs.txid.end(),
          [](std::byte a, std::byte b) {
            return std::to_integer<unsigned>(a) < std::to_integer<unsigned>(b);
          });
    }
    return lhs.index < rhs.index;
  }
};

// An unspent transaction output plus the consensus metadata needed later for
// rules such as coinbase maturity. Stored by value; cheap and immutable once
// created.
struct Coin {
  protocol::TxOutput output{};
  std::uint32_t height = 0;
  bool coinbase = false;

  [[nodiscard]] friend bool operator==(const Coin&, const Coin&) = default;
};

// The set of unspent transaction outputs (UTXOs).
//
// Invariants:
//   * An OutPoint is present at most once.
//   * Iteration order is deterministic (OutPointLess).
//
// This type owns chain state only; it performs no transaction *validation*
// (that lives in the validation layer) and has no knowledge of networking,
// storage, or policy.
class UtxoSet {
 public:
  [[nodiscard]] bool contains(const protocol::OutPoint& outpoint) const;

  // Returns a pointer to the coin, or nullptr if the outpoint is unspent-absent.
  // The pointer is valid until the next mutating operation.
  [[nodiscard]] const Coin* find(const protocol::OutPoint& outpoint) const;

  // Adds a new coin. Returns kInvalidStateTransition if the outpoint already
  // exists (creating a duplicate UTXO would be a consensus violation).
  [[nodiscard]] Result<void> add(const protocol::OutPoint& outpoint, const Coin& coin);

  // Spends (removes) a coin, returning it. Returns kInvalidStateTransition if
  // the outpoint is not present.
  [[nodiscard]] Result<Coin> spend(const protocol::OutPoint& outpoint);

  [[nodiscard]] std::size_t size() const noexcept { return coins_.size(); }
  [[nodiscard]] bool empty() const noexcept { return coins_.empty(); }

  // Deterministic, ordered view of the underlying coins for snapshotting and
  // tests.
  [[nodiscard]] const std::map<protocol::OutPoint, Coin, OutPointLess>& coins() const noexcept {
    return coins_;
  }

 private:
  std::map<protocol::OutPoint, Coin, OutPointLess> coins_;
};

}  // namespace blockchain::state

#endif  // BLOCKCHAIN_STATE_UTXO_SET_HPP
