#ifndef BLOCKCHAIN_MEMPOOL_RESTORE_HPP
#define BLOCKCHAIN_MEMPOOL_RESTORE_HPP

#include <span>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/mempool/policy.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"

namespace blockchain::mempool {

// Re-admits transactions from a ledger snapshot against `utxos`. Each entry must
// pass consensus and policy checks; failure indicates a corrupt snapshot.
[[nodiscard]] Result<void> restore_mempool(Mempool& pool, const state::UtxoSet& utxos,
                                           std::span<const protocol::Transaction> transactions,
                                           const MempoolPolicy& policy = {});

}  // namespace blockchain::mempool

#endif  // BLOCKCHAIN_MEMPOOL_RESTORE_HPP
