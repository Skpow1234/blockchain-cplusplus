#ifndef BLOCKCHAIN_PROTOCOL_BLOCK_HPP
#define BLOCKCHAIN_PROTOCOL_BLOCK_HPP

#include <cstdint>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"

namespace blockchain::protocol {

// Fixed-size header committing to the previous block and the transaction set.
struct BlockHeader {
  std::uint32_t version = kBlockVersion;
  crypto::Hash256 prev_block{};
  crypto::Hash256 merkle_root{};
  std::uint64_t timestamp = 0;
  std::uint32_t height = 0;

  void serialize(serialization::ByteWriter& writer) const;
  [[nodiscard]] static Result<BlockHeader> deserialize(serialization::ByteReader& reader);

  // Block hash = sha256d(canonical header encoding).
  [[nodiscard]] crypto::Hash256 hash() const;

  [[nodiscard]] friend bool operator==(const BlockHeader&, const BlockHeader&) = default;
};

struct Block {
  BlockHeader header{};
  std::vector<Transaction> transactions;

  void serialize(serialization::ByteWriter& writer) const;
  [[nodiscard]] static Result<Block> deserialize(serialization::ByteReader& reader);

  [[nodiscard]] std::vector<std::byte> to_bytes() const;

  [[nodiscard]] friend bool operator==(const Block&, const Block&) = default;
};

// Deterministic Merkle root over transaction ids. An empty transaction list
// yields the zero hash. Odd levels duplicate the final node (Bitcoin-style).
[[nodiscard]] crypto::Hash256 compute_merkle_root(const std::vector<Transaction>& transactions);

}  // namespace blockchain::protocol

#endif  // BLOCKCHAIN_PROTOCOL_BLOCK_HPP
