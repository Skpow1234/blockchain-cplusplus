#ifndef BLOCKCHAIN_PROTOCOL_CONSTANTS_HPP
#define BLOCKCHAIN_PROTOCOL_CONSTANTS_HPP

#include <cstdint>

// Documented, versioned protocol constants. These are the *only* values that
// are allowed to be hardcoded (per the project's no-hardcoding policy). Runtime
// behavior such as ports, peers, and paths must come from configuration.
namespace blockchain::protocol {

// Wire/consensus protocol version. Bump on any incompatible format change.
inline constexpr std::uint32_t kProtocolVersion = 1;

// Current serialized transaction and block versions.
inline constexpr std::uint32_t kTransactionVersion = 1;
inline constexpr std::uint32_t kBlockVersion = 1;

// Resource limits enforced during deserialization/validation to bound memory
// and CPU when handling untrusted input.
inline constexpr std::uint32_t kMaxBlockSizeBytes = 1U << 20U;        // 1 MiB
inline constexpr std::uint32_t kMaxTransactionSizeBytes = 1U << 16U;  // 64 KiB
inline constexpr std::uint32_t kMaxInputsPerTransaction = 4096;
inline constexpr std::uint32_t kMaxOutputsPerTransaction = 4096;
inline constexpr std::uint32_t kMaxTransactionsPerBlock = 1U << 16U;

// Maximum value any single output may carry. Kept well below the u64 ceiling
// so that sums of outputs cannot overflow within policy limits.
inline constexpr std::uint64_t kMaxMoney = 21'000'000ULL * 100'000'000ULL;

// Smallest indivisible unit per coin (like satoshis). Purely a denomination
// convention used by economic defaults below; it is not a consensus rule.
inline constexpr std::uint64_t kCoinUnit = 100'000'000ULL;

// Reserved output index that marks a coinbase input. A coinbase transaction has
// exactly one input whose prevout.index equals this sentinel; that input does
// not reference a real UTXO. The input's prevout.txid carries the block height
// so that the coinbase id is unique per height (no scriptSig exists yet).
inline constexpr std::uint32_t kNullPrevoutIndex = 0xFFFFFFFFU;

// Economic consensus defaults. These are documented protocol parameters; the
// runtime values are carried in consensus::ConsensusParams so simulations and
// tests can override them deterministically without touching code.
inline constexpr std::uint64_t kDefaultBlockSubsidy = 50ULL * kCoinUnit;
inline constexpr std::uint32_t kDefaultCoinbaseMaturity = 100;

}  // namespace blockchain::protocol

#endif  // BLOCKCHAIN_PROTOCOL_CONSTANTS_HPP
