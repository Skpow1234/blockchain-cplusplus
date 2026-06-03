#include <cstdint>

#include "blockchain/mempool/mempool.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "blockchain/validation/block_validation.hpp"
#include "testing.hpp"

using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::production::BlockTemplateParams;
using blockchain::production::build_block_template;
using blockchain::protocol::BlockHeader;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;
using blockchain::validation::connect_block;

namespace {

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

Transaction spend_tx(std::uint8_t tag, std::uint64_t out_value) {
  Transaction tx;
  TxInput input;
  input.prevout = make_outpoint(tag, 0);
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = out_value;
  tx.outputs.push_back(output);
  return tx;
}

BlockTemplateParams params(std::uint32_t max_size, std::uint32_t max_txs) {
  return BlockTemplateParams{
      .version = 1, .max_block_size_bytes = max_size, .max_transactions = max_txs};
}

// Builds a UTXO set funding outpoints (0x10,0) and (0x11,0) with 1000 each, and
// a mempool holding a low-fee tx (fee 10) and a high-fee tx (fee 900).
struct Fixture {
  UtxoSet utxos;
  Mempool pool{MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U}};
  Transaction low = spend_tx(0x10, 990);
  Transaction high = spend_tx(0x11, 100);

  Fixture() {
    Coin coin;
    coin.output.value = 1000;
    (void)utxos.add(make_outpoint(0x10, 0), coin);
    (void)utxos.add(make_outpoint(0x11, 0), coin);
    (void)pool.accept(low, utxos);
    (void)pool.accept(high, utxos);
  }
};

}  // namespace

TEST_CASE("empty mempool yields an empty but valid block") {
  const UtxoSet utxos;
  const Mempool pool{MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U}};
  const BlockHeader tip;  // genesis-like: height 0

  auto tmpl = build_block_template(tip, 1000, pool, utxos, params(1U << 20U, 100));
  CHECK(tmpl.has_value());
  CHECK_EQ(tmpl->selected_count, static_cast<std::size_t>(0));
  CHECK_EQ(tmpl->total_fees, static_cast<std::uint64_t>(0));
  CHECK_EQ(tmpl->block.header.height, static_cast<std::uint32_t>(1));
  CHECK(tmpl->block.header.prev_block == tip.hash());
}

TEST_CASE("template includes all fitting transactions and sums fees") {
  const Fixture fx;
  const BlockHeader tip;

  auto tmpl = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 100));
  CHECK(tmpl.has_value());
  CHECK_EQ(tmpl->selected_count, static_cast<std::size_t>(2));
  CHECK_EQ(tmpl->total_fees, static_cast<std::uint64_t>(910));
}

TEST_CASE("coinbase is first and the highest fee-rate transaction follows") {
  const Fixture fx;
  const BlockHeader tip;

  auto tmpl = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 100));
  CHECK(tmpl.has_value());
  CHECK(tmpl->block.transactions.front().is_coinbase());
  // The first *selected* (non-coinbase) transaction is the higher fee-rate one.
  CHECK(tmpl->block.transactions.at(1).txid() == fx.high.txid());
}

TEST_CASE("count budget selects only the best transaction") {
  const Fixture fx;
  const BlockHeader tip;

  auto tmpl = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 1));
  CHECK(tmpl.has_value());
  CHECK_EQ(tmpl->selected_count, static_cast<std::size_t>(1));
  CHECK(tmpl->block.transactions.front().txid() == fx.high.txid());
  CHECK_EQ(tmpl->total_fees, static_cast<std::uint64_t>(900));
}

TEST_CASE("size budget selects only the best transaction") {
  const Fixture fx;
  const BlockHeader tip;
  // Header(80) + count(4) = 84 base; each 1-in/1-out tx is 92 bytes.
  // A 200-byte budget admits exactly one transaction (84 + 92 = 176).
  auto tmpl = build_block_template(tip, 1000, fx.pool, fx.utxos, params(200, 100));
  CHECK(tmpl.has_value());
  CHECK_EQ(tmpl->selected_count, static_cast<std::size_t>(1));
  CHECK(tmpl->block.transactions.front().txid() == fx.high.txid());
}

TEST_CASE("produced block validates against the same UTXO set") {
  const Fixture fx;
  const BlockHeader tip;

  auto tmpl = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 100));
  CHECK(tmpl.has_value());

  UtxoSet verify = fx.utxos;
  auto connected = connect_block(tmpl->block, verify);
  CHECK(connected.has_value());
  CHECK_EQ(connected.value(), tmpl->total_fees);
}

TEST_CASE("template is deterministic") {
  const Fixture fx;
  const BlockHeader tip;

  auto a = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 100));
  auto b = build_block_template(tip, 1000, fx.pool, fx.utxos, params(1U << 20U, 100));
  CHECK(a.has_value());
  CHECK(b.has_value());
  CHECK(a->block == b->block);
}
