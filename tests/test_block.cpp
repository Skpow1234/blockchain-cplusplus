#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "testing.hpp"

using blockchain::protocol::Block;
using blockchain::protocol::BlockHeader;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxOutput;
using blockchain::serialization::ByteReader;

namespace {

Transaction tx_with_value(std::uint64_t value) {
  Transaction tx;
  TxOutput output;
  output.value = value;
  tx.outputs.push_back(output);
  return tx;
}

}  // namespace

TEST_CASE("block header round-trip") {
  BlockHeader header;
  header.version = 1;
  header.prev_block.fill(std::byte{0x11});
  header.merkle_root.fill(std::byte{0x22});
  header.timestamp = 1234567890;
  header.height = 5;

  blockchain::serialization::ByteWriter writer;
  header.serialize(writer);
  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  auto decoded = BlockHeader::deserialize(reader);
  CHECK(decoded.has_value());
  CHECK(reader.expect_end().has_value());
  CHECK(decoded.value() == header);
}

TEST_CASE("empty merkle root is the zero hash") {
  CHECK(compute_merkle_root({}) == blockchain::crypto::zero_hash());
}

TEST_CASE("single-transaction merkle root equals the txid") {
  const Transaction tx = tx_with_value(100);
  const std::vector<Transaction> txs = {tx};
  CHECK(compute_merkle_root(txs) == tx.txid());
}

TEST_CASE("merkle root is deterministic and order-sensitive") {
  const std::vector<Transaction> ordered = {tx_with_value(1), tx_with_value(2), tx_with_value(3)};
  const std::vector<Transaction> reordered = {tx_with_value(3), tx_with_value(2), tx_with_value(1)};
  CHECK(compute_merkle_root(ordered) == compute_merkle_root(ordered));
  CHECK(compute_merkle_root(ordered) != compute_merkle_root(reordered));
}

TEST_CASE("block round-trip with transactions") {
  Block block;
  block.header.timestamp = 42;
  block.transactions = {tx_with_value(10), tx_with_value(20)};
  block.header.merkle_root = compute_merkle_root(block.transactions);

  const auto bytes = block.to_bytes();
  ByteReader reader(std::span<const std::byte>(bytes.data(), bytes.size()));
  auto decoded = Block::deserialize(reader);
  CHECK(decoded.has_value());
  CHECK(reader.expect_end().has_value());
  CHECK(decoded.value() == block);
}
