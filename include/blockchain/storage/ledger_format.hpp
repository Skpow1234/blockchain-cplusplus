#ifndef BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP
#define BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP

#include <cstdint>

// On-disk ledger format for restart recovery (Stage 3).
//
// File: <data_dir>/ledger.bin (written atomically via ledger.bin.tmp)
//
// Layout (little-endian, canonical, no trailing bytes before checksum):
//   magic            u32   kLedgerMagic
//   format_version   u32   kLedgerFormatVersion
//   block_subsidy    u64
//   coinbase_maturity u32
//   block_count      u32   number of blocks (genesis first), <= kMaxLedgerBlocks
//   blocks           repeated block_count times:
//                      block_bytes  u32 len + bytes (canonical Block encoding)
//   checksum         u32   first 4 bytes of SHA-256(all preceding fields)
//
// Recovery replays every stored block through Chain::create / submit_block.
// Corrupt, truncated, or checksum-mismatched files are rejected.
namespace blockchain::storage {

inline constexpr std::uint32_t kLedgerMagic = 0xBC570001U;
inline constexpr std::uint32_t kLedgerFormatVersion = 1U;

// Upper bound on blocks in one ledger file (resource limit, not a consensus rule).
inline constexpr std::uint32_t kMaxLedgerBlocks = 1'000'000U;

[[nodiscard]] constexpr const char* ledger_filename() noexcept { return "ledger.bin"; }
[[nodiscard]] constexpr const char* ledger_temp_filename() noexcept { return "ledger.bin.tmp"; }

}  // namespace blockchain::storage

#endif  // BLOCKCHAIN_STORAGE_LEDGER_FORMAT_HPP
