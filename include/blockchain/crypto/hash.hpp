#ifndef BLOCKCHAIN_CRYPTO_HASH_HPP
#define BLOCKCHAIN_CRYPTO_HASH_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace blockchain::crypto {

// A 256-bit hash digest. Used for transaction ids, block hashes, and Merkle
// nodes. Stored as raw bytes; rendering to hex is explicit and big-endian
// (most-significant byte first) to match common block-explorer conventions.
using Hash256 = std::array<std::byte, 32>;

// Deterministic SHA-256 over an arbitrary byte range.
[[nodiscard]] Hash256 sha256(std::span<const std::byte> data);

// Double SHA-256 (SHA-256 of SHA-256). Used for ids/commitments to harden
// against length-extension style issues, mirroring common chain designs.
[[nodiscard]] Hash256 sha256d(std::span<const std::byte> data);

// Lowercase hex encoding of a digest.
[[nodiscard]] std::string to_hex(const Hash256& hash);

// All-zero digest constant (e.g. the genesis block's "previous" reference).
[[nodiscard]] constexpr Hash256 zero_hash() noexcept {
  return Hash256{};
}

}  // namespace blockchain::crypto

#endif  // BLOCKCHAIN_CRYPTO_HASH_HPP
