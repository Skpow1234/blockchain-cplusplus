#ifndef BLOCKCHAIN_STORAGE_CHAIN_STORE_HPP
#define BLOCKCHAIN_STORAGE_CHAIN_STORE_HPP

#include <span>
#include <string>
#include <vector>

#include "blockchain/consensus/chain.hpp"
#include "blockchain/consensus/params.hpp"
#include "blockchain/error.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/transaction.hpp"

namespace blockchain::storage {

// Validated ledger contents loaded from disk.
struct LedgerData {
  std::vector<protocol::Block> blocks;
  consensus::ConsensusParams params;
  std::vector<protocol::Transaction> mempool_transactions;
};

// Persists the canonical block sequence for a node and reloads it by full
// consensus replay. The block list is the source of truth; nothing is trusted
// without re-validation on load.
class ChainStore {
 public:
  explicit ChainStore(std::string data_dir);

  [[nodiscard]] const std::string& data_dir() const noexcept { return data_dir_; }

  [[nodiscard]] static std::string ledger_path(const std::string& data_dir);
  [[nodiscard]] static std::string ledger_temp_path(const std::string& data_dir);

  [[nodiscard]] bool ledger_exists() const;

  // Writes ledger.bin atomically. `blocks` must be height-ordered from genesis.
  [[nodiscard]] Result<void> save_ledger(std::span<const protocol::Block> blocks,
                                         const consensus::ConsensusParams& params,
                                         std::span<const protocol::Transaction> mempool = {});

  // Deserializes ledger.bin and rebuilds chain state by replaying every block.
  [[nodiscard]] Result<consensus::Chain> load_chain();

  // Returns validated blocks, consensus params, and optional mempool snapshot.
  [[nodiscard]] Result<LedgerData> load_ledger();

  // Serializes or deserializes the ledger wire format (for tests and fuzzing).
  [[nodiscard]] static Result<std::vector<std::byte>> encode_ledger(
      std::span<const protocol::Block> blocks, const consensus::ConsensusParams& params,
      std::span<const protocol::Transaction> mempool = {});
  [[nodiscard]] static Result<LedgerData> decode_ledger(std::span<const std::byte> bytes);

  // Test helper: checksum over ledger body bytes (everything before the checksum field).
  [[nodiscard]] static std::uint32_t ledger_body_checksum(std::span<const std::byte> body);
  [[nodiscard]] static std::vector<std::byte> with_ledger_checksum(std::span<const std::byte> body);

 private:
  std::string data_dir_;
};

}  // namespace blockchain::storage

#endif  // BLOCKCHAIN_STORAGE_CHAIN_STORE_HPP
