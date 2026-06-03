#include <cstdint>
#include <utility>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "blockchain/validation/block_validation.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::protocol::Block;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;
using blockchain::validation::check_block_sanity;
using blockchain::validation::connect_block;

namespace {

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

Transaction spend_tx(std::uint8_t tag, std::uint32_t index, std::uint64_t out_value,
                     std::uint8_t recipient_tag) {
  Transaction tx;
  TxInput input;
  input.prevout = make_outpoint(tag, index);
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = out_value;
  output.recipient.fill(std::byte{recipient_tag});
  tx.outputs.push_back(output);
  return tx;
}

UtxoSet funded(std::uint8_t tag, std::uint32_t index, std::uint64_t value) {
  UtxoSet set;
  Coin coin;
  coin.output.value = value;
  (void)set.add(make_outpoint(tag, index), coin);
  return set;
}

// Wraps transactions into a block with a correct Merkle root.
Block make_block(std::vector<Transaction> txs, std::uint32_t height) {
  Block block;
  block.header.height = height;
  block.transactions = std::move(txs);
  block.header.merkle_root = compute_merkle_root(block.transactions);
  return block;
}

}  // namespace

TEST_CASE("empty block passes sanity") {
  const Block block = make_block({}, 1);
  CHECK(check_block_sanity(block).has_value());
}

TEST_CASE("wrong merkle root is rejected") {
  Block block = make_block({spend_tx(0x01, 0, 10, 0xAA)}, 1);
  block.header.merkle_root.fill(std::byte{0xFF});
  auto result = check_block_sanity(block);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("duplicate transaction in block is rejected") {
  const Transaction tx = spend_tx(0x02, 0, 10, 0xAA);
  const Block block = make_block({tx, tx}, 1);
  auto result = check_block_sanity(block);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("connect applies state transition and returns fees") {
  UtxoSet set = funded(0x03, 0, 100);
  const Transaction tx = spend_tx(0x03, 0, 70, 0xAA);  // fee 30
  const Block block = make_block({tx}, 5);

  auto fees = connect_block(block, set);
  CHECK(fees.has_value());
  CHECK_EQ(fees.value(), static_cast<std::uint64_t>(30));

  // Spent input is gone; the new output exists at the block height.
  CHECK(!set.contains(make_outpoint(0x03, 0)));
  OutPoint created;
  created.txid = tx.txid();
  created.index = 0;
  const Coin* coin = set.find(created);
  CHECK(coin != nullptr);
  CHECK_EQ(coin->output.value, static_cast<std::uint64_t>(70));
  CHECK_EQ(coin->height, static_cast<std::uint32_t>(5));
}

TEST_CASE("connect allows spending an output created earlier in the same block") {
  UtxoSet set = funded(0x04, 0, 100);
  const Transaction first = spend_tx(0x04, 0, 90, 0xAA);  // creates first:0 = 90
  Transaction second;
  TxInput in;
  in.prevout.txid = first.txid();
  in.prevout.index = 0;
  second.inputs.push_back(in);
  TxOutput out;
  out.value = 80;
  second.outputs.push_back(out);  // fee 10

  const Block block = make_block({first, second}, 2);
  auto fees = connect_block(block, set);
  CHECK(fees.has_value());
  CHECK_EQ(fees.value(), static_cast<std::uint64_t>(20));  // 10 + 10
}

TEST_CASE("connect rejects forward reference within the same block") {
  UtxoSet set = funded(0x05, 0, 100);
  const Transaction producer = spend_tx(0x05, 0, 90, 0xAA);
  Transaction consumer;
  TxInput in;
  in.prevout.txid = producer.txid();
  in.prevout.index = 0;
  consumer.inputs.push_back(in);
  TxOutput out;
  out.value = 80;
  consumer.outputs.push_back(out);

  // consumer placed BEFORE producer -> its input does not exist yet.
  const Block block = make_block({consumer, producer}, 2);
  auto fees = connect_block(block, set);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("connect is all-or-nothing on failure") {
  UtxoSet set = funded(0x06, 0, 100);
  // Second tx is invalid (missing input); the whole block must not apply.
  const Transaction good = spend_tx(0x06, 0, 70, 0xAA);
  const Transaction bad = spend_tx(0x07, 0, 10, 0xBB);  // 0x07 not funded
  const Block block = make_block({good, bad}, 1);

  auto fees = connect_block(block, set);
  CHECK(!fees.has_value());
  // Original UTXO untouched: the funded outpoint is still present and unspent.
  CHECK(set.contains(make_outpoint(0x06, 0)));
  CHECK_EQ(set.size(), static_cast<std::size_t>(1));
}
