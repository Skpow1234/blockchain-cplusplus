#include "blockchain/state/utxo_set.hpp"

#include "blockchain/crypto/hash.hpp"

namespace blockchain::state {

bool UtxoSet::contains(const protocol::OutPoint& outpoint) const {
  return coins_.find(outpoint) != coins_.end();
}

const Coin* UtxoSet::find(const protocol::OutPoint& outpoint) const {
  const auto it = coins_.find(outpoint);
  return it == coins_.end() ? nullptr : &it->second;
}

Result<void> UtxoSet::add(const protocol::OutPoint& outpoint, const Coin& coin) {
  const auto [it, inserted] = coins_.try_emplace(outpoint, coin);
  if (!inserted) {
    return make_error(ErrorCode::kInvalidStateTransition, "duplicate UTXO for outpoint " +
                                                              crypto::to_hex(outpoint.txid) + ":" +
                                                              std::to_string(outpoint.index));
  }
  return {};
}

Result<Coin> UtxoSet::spend(const protocol::OutPoint& outpoint) {
  const auto it = coins_.find(outpoint);
  if (it == coins_.end()) {
    return make_error(ErrorCode::kInvalidStateTransition, "spend of unknown outpoint " +
                                                              crypto::to_hex(outpoint.txid) + ":" +
                                                              std::to_string(outpoint.index));
  }
  Coin spent = it->second;
  coins_.erase(it);
  return spent;
}

}  // namespace blockchain::state
