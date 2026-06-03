#include <cstdint>
#include <utility>
#include <vector>

#include "blockchain/consensus/params.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "blockchain/validation/block_validation.hpp"
#include "blockchain/validation/transaction_validation.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::consensus::ConsensusParams;
using blockchain::crypto::Hash256;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::protocol::Block;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::kNullPrevoutIndex;
using blockchain::protocol::make_coinbase;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;
using blockchain::validation::check_block_sanity;
using blockchain::validation::check_transaction_sanity;
using blockchain::validation::connect_block;

namespace {

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid = tag_hash(tag);
  out.index = index;
  return out;
}

Transaction spend_tx(const OutPoint& prevout, std::uint64_t value) {
  Transaction tx;
  TxInput input;
  input.prevout = prevout;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = value;
  tx.outputs.push_back(output);
  return tx;
}

Block make_block(std::vector<Transaction> txs, std::uint32_t height) {
  Block block;
  block.header.height = height;
  block.transactions = std::move(txs);
  block.header.merkle_root = compute_merkle_root(block.transactions);
  return block;
}

// Adds a coin (coinbase or not) directly to a UTXO set for spend tests.
void fund(UtxoSet& set, const OutPoint& outpoint, std::uint64_t value, std::uint32_t height,
          bool coinbase) {
  Coin coin;
  coin.output.value = value;
  coin.height = height;
  coin.coinbase = coinbase;
  (void)set.add(outpoint, coin);
}

constexpr std::uint64_t kSubsidy = 50;

}  // namespace

TEST_CASE("make_coinbase is recognized and unique per height") {
  const Transaction cb1 = make_coinbase(1, kSubsidy, tag_hash(0xAA));
  const Transaction cb2 = make_coinbase(2, kSubsidy, tag_hash(0xAA));
  CHECK(cb1.is_coinbase());
  CHECK(cb2.is_coinbase());
  CHECK(cb1.txid() != cb2.txid());  // height is encoded into the coinbase id

  const Transaction normal = spend_tx(make_outpoint(0x01, 0), 10);
  CHECK(!normal.is_coinbase());
}

TEST_CASE("coinbase passes sanity but a non-coinbase null prevout is rejected") {
  CHECK(check_transaction_sanity(make_coinbase(1, kSubsidy, tag_hash(0xAA))).has_value());

  // Two inputs (so not a coinbase) where one uses the reserved sentinel index.
  Transaction tx = spend_tx(make_outpoint(0x01, 0), 10);
  TxInput bogus;
  bogus.prevout = make_outpoint(0x02, kNullPrevoutIndex);
  tx.inputs.push_back(bogus);
  auto sane = check_transaction_sanity(tx);
  CHECK(!sane.has_value());
  CHECK(sane.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("mempool rejects a coinbase transaction") {
  const UtxoSet utxos;
  Mempool pool(MempoolLimits{.max_transactions = 16, .max_total_bytes = 1U << 20U});
  auto accepted = pool.accept(make_coinbase(1, kSubsidy, tag_hash(0xAA)), utxos);
  CHECK(!accepted.has_value());
  CHECK(accepted.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("connect_block mints the subsidy through the coinbase") {
  UtxoSet set;
  const ConsensusParams params{.block_subsidy = kSubsidy, .coinbase_maturity = 100};
  const Transaction cb = make_coinbase(1, kSubsidy, tag_hash(0xAA));
  const Block block = make_block({cb}, 1);

  auto fees = connect_block(block, set, params);
  CHECK(fees.has_value());
  CHECK_EQ(fees.value(), static_cast<std::uint64_t>(0));

  OutPoint created;
  created.txid = cb.txid();
  created.index = 0;
  const Coin* coin = set.find(created);
  CHECK(coin != nullptr);
  CHECK_EQ(coin->output.value, kSubsidy);
  CHECK(coin->coinbase);
  CHECK_EQ(coin->height, static_cast<std::uint32_t>(1));
}

TEST_CASE("coinbase paying more than subsidy plus fees is rejected") {
  UtxoSet set;
  const ConsensusParams params{.block_subsidy = kSubsidy, .coinbase_maturity = 100};
  const Block block = make_block({make_coinbase(1, kSubsidy + 1, tag_hash(0xAA))}, 1);

  auto fees = connect_block(block, set, params);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("coinbase may claim the block's fees") {
  UtxoSet set;
  fund(set, make_outpoint(0x33, 0), 100, /*height=*/0, /*coinbase=*/false);
  const ConsensusParams params{.block_subsidy = kSubsidy, .coinbase_maturity = 100};

  const Transaction payer = spend_tx(make_outpoint(0x33, 0), 70);  // fee = 30
  // Coinbase claims subsidy + the 30 fee.
  const Block ok = make_block({make_coinbase(1, kSubsidy + 30, tag_hash(0xAA)), payer}, 1);
  auto fees = connect_block(ok, set, params);
  CHECK(fees.has_value());
  CHECK_EQ(fees.value(), static_cast<std::uint64_t>(30));

  // Claiming one more than subsidy + fee is rejected.
  UtxoSet set2;
  fund(set2, make_outpoint(0x33, 0), 100, 0, false);
  const Block bad = make_block({make_coinbase(1, kSubsidy + 31, tag_hash(0xAA)), payer}, 1);
  CHECK(!connect_block(bad, set2, params).has_value());
}

TEST_CASE("coinbase outputs cannot be spent before maturity") {
  const ConsensusParams params{.block_subsidy = kSubsidy, .coinbase_maturity = 2};

  // A coinbase coin created at height 5 is spendable only at height >= 7.
  const OutPoint cb_out = make_outpoint(0x77, 0);
  const Transaction spend = spend_tx(cb_out, 40);

  UtxoSet immature;
  fund(immature, cb_out, 50, /*height=*/5, /*coinbase=*/true);
  const Block too_early = make_block({spend}, 6);
  auto early = connect_block(too_early, immature, params);
  CHECK(!early.has_value());
  CHECK(early.error().code == ErrorCode::kInvalidTransaction);

  UtxoSet mature;
  fund(mature, cb_out, 50, /*height=*/5, /*coinbase=*/true);
  const Block on_time = make_block({spend}, 7);
  CHECK(connect_block(on_time, mature, params).has_value());
}

TEST_CASE("a coinbase that is not the first transaction is rejected") {
  const Transaction normal = spend_tx(make_outpoint(0x44, 0), 10);
  const Block block = make_block({normal, make_coinbase(1, kSubsidy, tag_hash(0xAA))}, 1);
  auto sane = check_block_sanity(block);
  CHECK(!sane.has_value());
  CHECK(sane.error().code == ErrorCode::kInvalidBlock);
}
