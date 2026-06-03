#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/mempool/mempool.hpp"
#include "blockchain/mempool/policy.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "blockchain/state/utxo_set.hpp"

// libFuzzer entry point. Goal: mempool admission must never crash, leak, or hit UB
// on arbitrary transaction bytes (after deserialization succeeds).
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  blockchain::serialization::ByteReader reader(bytes);

  auto tx = blockchain::protocol::Transaction::deserialize(reader);
  if (!tx.has_value()) {
    return 0;
  }

  blockchain::state::UtxoSet utxos;
  blockchain::mempool::Mempool pool(
      blockchain::mempool::MempoolLimits{.max_transactions = 128, .max_total_bytes = 1U << 20U});
  blockchain::mempool::MempoolPolicy policy;
  (void)blockchain::mempool::validate_for_mempool(*tx, utxos, policy);
  (void)pool.accept(*tx, utxos, policy);
  return 0;
}
