#ifndef BLOCKCHAIN_PROTOCOL_TRANSACTION_HPP
#define BLOCKCHAIN_PROTOCOL_TRANSACTION_HPP

#include <cstdint>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"

namespace blockchain::protocol {

// Reference to a previous transaction output being spent.
struct OutPoint {
  crypto::Hash256 txid{};
  std::uint32_t index = 0;

  [[nodiscard]] friend bool operator==(const OutPoint&, const OutPoint&) = default;
};

// A spend of a previous output. Signatures/scripts are intentionally omitted at
// Stage 1; authorization will be added behind this type later.
struct TxInput {
  OutPoint prevout{};

  [[nodiscard]] friend bool operator==(const TxInput&, const TxInput&) = default;
};

// A newly created output: an amount assigned to a recipient identifier (a
// 32-byte public-key hash placeholder for now).
struct TxOutput {
  std::uint64_t value = 0;
  crypto::Hash256 recipient{};

  [[nodiscard]] friend bool operator==(const TxOutput&, const TxOutput&) = default;
};

// A transaction. Encoded canonically; the id is the double-SHA-256 of the
// canonical encoding.
struct Transaction {
  std::uint32_t version = kTransactionVersion;
  std::vector<TxInput> inputs;
  std::vector<TxOutput> outputs;
  std::uint32_t lock_time = 0;

  void serialize(serialization::ByteWriter& writer) const;

  // Parses a transaction from a reader. Structural limits are enforced, but
  // higher-level semantic validation is performed separately.
  [[nodiscard]] static Result<Transaction> deserialize(serialization::ByteReader& reader);

  // Canonical byte encoding.
  [[nodiscard]] std::vector<std::byte> to_bytes() const;

  // Transaction id = sha256d(canonical encoding).
  [[nodiscard]] crypto::Hash256 txid() const;

  [[nodiscard]] friend bool operator==(const Transaction&, const Transaction&) = default;
};

}  // namespace blockchain::protocol

#endif  // BLOCKCHAIN_PROTOCOL_TRANSACTION_HPP
