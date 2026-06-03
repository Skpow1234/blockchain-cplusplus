#ifndef BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP
#define BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP

#include <cstdint>

// On-disk ledger format for restart recovery (Stage 4).
//
// File: <data_dir>/ledger.bin (written atomically via ledger.bin.tmp)
//
// Format version 1 layout (little-endian, canonical, no trailing bytes before checksum):
//   magic            u32   kLedgerMagic
//   format_version   u32   1
//   block_subsidy    u64
//   coinbase_maturity u32
//   block_count      u32   number of blocks (genesis first), <= kMaxLedgerBlocks
//   blocks           repeated block_count times:
//                      block_bytes  u32 len + bytes (canonical Block encoding)
//   checksum         u32   first 4 bytes of SHA-256(all preceding fields)
//
// Format version 2 adds a mempool snapshot after blocks (before checksum):
//   mempool_tx_count u32   <= kMaxLedgerMempoolTransactions
//   mempool_txs      repeated mempool_tx_count times:
//                      tx_bytes  u32 len + bytes (canonical Transaction encoding)
//
// Recovery replays every stored block through Chain::create / submit_block, then
// re-validates and re-admits each stored mempool transaction. Corrupt, truncated,
// or checksum-mismatched files are rejected.
namespace blockchain::storage {

inline constexpr std::uint32_t kLedgerMagic = 0xBC570001U;
inline constexpr std::uint32_t kLedgerFormatVersion = 2U;
inline constexpr std::uint32_t kLedgerFormatVersion1 = 1U;

// Upper bound on blocks in one ledger file (resource limit, not a consensus rule).
inline constexpr std::uint32_t kMaxLedgerBlocks = 1'000'000U;
inline constexpr std::uint32_t kMaxLedgerMempoolTransactions = 10'000U;

[[nodiscard]] constexpr const char* ledger_filename() noexcept {
  return "ledger.bin";
}
[[nodiscard]] constexpr const char* ledger_temp_filename() noexcept {
  return "ledger.bin.tmp";
}

}  // namespace blockchain::storage

#endif  // BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP
