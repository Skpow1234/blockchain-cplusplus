#include <cstdint>
#include <span>

#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "testing.hpp"

using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::serialization::ByteReader;
using blockchain::serialization::ByteWriter;

namespace {

Transaction sample_tx() {
  Transaction tx;
  tx.version = 1;
  TxInput input;
  input.prevout.txid.fill(std::byte{0xAB});
  input.prevout.index = 7;
  tx.inputs.push_back(input);

  TxOutput output;
  output.value = 42;
  output.recipient.fill(std::byte{0xCD});
  tx.outputs.push_back(output);

  tx.lock_time = 0;
  return tx;
}

}  // namespace

TEST_CASE("transaction serialize/deserialize round-trip") {
  const Transaction tx = sample_tx();
  const auto bytes = tx.to_bytes();

  ByteReader reader(std::span<const std::byte>(bytes.data(), bytes.size()));
  auto decoded = Transaction::deserialize(reader);
  CHECK(decoded.has_value());
  CHECK(reader.expect_end().has_value());
  CHECK(decoded.value() == tx);
}

TEST_CASE("txid is deterministic and encoding-stable") {
  const Transaction a = sample_tx();
  const Transaction b = sample_tx();
  CHECK_EQ(blockchain::crypto::to_hex(a.txid()), blockchain::crypto::to_hex(b.txid()));
  CHECK(a.to_bytes() == b.to_bytes());
}

TEST_CASE("different transactions have different ids") {
  Transaction a = sample_tx();
  Transaction b = sample_tx();
  b.outputs[0].value = 43;
  CHECK_NE(blockchain::crypto::to_hex(a.txid()), blockchain::crypto::to_hex(b.txid()));
}

TEST_CASE("truncated transaction fails to deserialize") {
  const Transaction tx = sample_tx();
  auto bytes = tx.to_bytes();
  bytes.resize(bytes.size() - 4);  // drop the lock_time
  ByteReader reader(std::span<const std::byte>(bytes.data(), bytes.size()));
  auto decoded = Transaction::deserialize(reader);
  CHECK(!decoded.has_value());
}
