#include "blockchain/mempool/restore.hpp"

namespace blockchain::mempool {

Result<void> restore_mempool(Mempool& pool, const state::UtxoSet& utxos,
                             const std::span<const protocol::Transaction> transactions,
                             const MempoolPolicy& policy) {
  for (const protocol::Transaction& tx : transactions) {
    if (auto ok = pool.accept(tx, utxos, policy); !ok) {
      return make_error(ErrorCode::kStorageCorruption,
                        "stored mempool transaction failed re-validation: " + ok.error().message);
    }
  }
  return {};
}

}  // namespace blockchain::mempool
