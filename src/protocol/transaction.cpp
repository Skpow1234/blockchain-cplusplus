#include "blockchain/protocol/transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/protocol/constants.hpp"

namespace blockchain::protocol {

void Transaction::serialize(serialization::ByteWriter& writer) const {
  writer.put_u32(version);

  writer.put_u32(static_cast<std::uint32_t>(inputs.size()));
  for (const TxInput& input : inputs) {
    writer.put_hash(input.prevout.txid);
    writer.put_u32(input.prevout.index);
  }

  writer.put_u32(static_cast<std::uint32_t>(outputs.size()));
  for (const TxOutput& output : outputs) {
    writer.put_u64(output.value);
    writer.put_hash(output.recipient);
  }

  writer.put_u32(lock_time);
}

Result<Transaction> Transaction::deserialize(serialization::ByteReader& reader) {
  Transaction tx;

  auto version = reader.get_u32();
  if (!version) {
    return std::unexpected(version.error());
  }
  tx.version = *version;

  auto input_count = reader.get_length(kMaxInputsPerTransaction);
  if (!input_count) {
    return std::unexpected(input_count.error());
  }
  tx.inputs.reserve(*input_count);
  for (std::uint32_t i = 0; i < *input_count; ++i) {
    TxInput input;
    auto txid = reader.get_hash();
    if (!txid) {
      return std::unexpected(txid.error());
    }
    auto index = reader.get_u32();
    if (!index) {
      return std::unexpected(index.error());
    }
    input.prevout.txid = *txid;
    input.prevout.index = *index;
    tx.inputs.push_back(input);
  }

  auto output_count = reader.get_length(kMaxOutputsPerTransaction);
  if (!output_count) {
    return std::unexpected(output_count.error());
  }
  tx.outputs.reserve(*output_count);
  for (std::uint32_t i = 0; i < *output_count; ++i) {
    TxOutput output;
    auto value = reader.get_u64();
    if (!value) {
      return std::unexpected(value.error());
    }
    auto recipient = reader.get_hash();
    if (!recipient) {
      return std::unexpected(recipient.error());
    }
    output.value = *value;
    output.recipient = *recipient;
    tx.outputs.push_back(output);
  }

  auto lock_time = reader.get_u32();
  if (!lock_time) {
    return std::unexpected(lock_time.error());
  }
  tx.lock_time = *lock_time;

  return tx;
}

std::vector<std::byte> Transaction::to_bytes() const {
  serialization::ByteWriter writer;
  serialize(writer);
  return writer.data();
}

crypto::Hash256 Transaction::txid() const {
  const std::vector<std::byte> bytes = to_bytes();
  return crypto::sha256d(std::span<const std::byte>(bytes.data(), bytes.size()));
}

bool Transaction::is_coinbase() const noexcept {
  return inputs.size() == 1 && inputs[0].prevout.index == kNullPrevoutIndex;
}

Transaction make_coinbase(std::uint32_t height, std::uint64_t value,
                          const crypto::Hash256& recipient) {
  Transaction tx;
  TxInput input;
  // Encode the height (little-endian) into the leading bytes of the otherwise
  // unused coinbase outpoint txid to keep per-height coinbase ids distinct.
  input.prevout.txid = crypto::Hash256{};
  for (std::size_t i = 0; i < sizeof(height); ++i) {
    input.prevout.txid[i] = static_cast<std::byte>((height >> (8U * i)) & 0xFFU);
  }
  input.prevout.index = kNullPrevoutIndex;
  tx.inputs.push_back(input);

  TxOutput output;
  output.value = value;
  output.recipient = recipient;
  tx.outputs.push_back(output);
  return tx;
}

}  // namespace blockchain::protocol
