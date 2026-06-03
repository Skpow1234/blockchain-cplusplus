#ifndef BLOCKCHAIN_MEMPOOL_POLICY_HPP
#define BLOCKCHAIN_MEMPOOL_POLICY_HPP

#include <cstdint>

#include "blockchain/error.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"

namespace blockchain::mempool {

// Documented simulator/relay defaults for mempool capacity. Runtime values come
// from NodeConfig (zero fields select these defaults).
inline constexpr std::uint32_t kDefaultMempoolMaxTransactions = 10'000;
inline constexpr std::uint64_t kDefaultMempoolMaxBytes =
    100ULL * static_cast<std::uint64_t>(protocol::kMaxBlockSizeBytes);

// Mempool acceptance policy (distinct from consensus validity). A transaction
// may be consensus-valid yet rejected here (e.g. below min relay feerate).
struct MempoolPolicy {
  // Minimum fee per serialized byte. Zero disables the check.
  std::uint64_t min_relay_feerate = 0;
};

struct MempoolAdmission {
  std::uint64_t fee = 0;
  std::uint64_t size_bytes = 0;
};

// Runs consensus validation then mempool policy checks. Does not inspect the
// in-memory pool (conflicts/limits are enforced by Mempool::accept).
[[nodiscard]] Result<MempoolAdmission> validate_for_mempool(const protocol::Transaction& tx,
                                                            const state::UtxoSet& utxos,
                                                            const MempoolPolicy& policy);

}  // namespace blockchain::mempool

#endif  // BLOCKCHAIN_MEMPOOL_POLICY_HPP
