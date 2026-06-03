// Stage-3 integration: policy vs consensus, deterministic block production.
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/mempool/policy.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/node/simulator.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::crypto::Hash256;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::mempool::MempoolPolicy;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::node::run_simulator;
using blockchain::node::SimulatorOptions;
using blockchain::node::SimulatorStep;
using blockchain::production::BlockTemplateParams;
using blockchain::production::build_block_template;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;

namespace {

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index = 0) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

Transaction spend_tx(const OutPoint& prevout, std::uint64_t out_value) {
  Transaction tx;
  TxInput input;
  input.prevout = prevout;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = out_value;
  tx.outputs.push_back(output);
  return tx;
}

NodeConfig stage3_config() {
  NodeConfig config;
  config.genesis_timestamp = 1'700'000'200;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex =
      "a100000000000000000000000000000000000000000000000000000000000000";
  config.mempool_max_transactions = 100;
  config.mempool_max_bytes = 1U << 20U;
  return config;
}

Transaction coinbase_spend_after_block1(const NodeConfig& config) {
  NodeConfig miner = config;
  miner.mine_blocks = 1;
  auto state = PeerState::from_config(miner);
  CHECK(state.has_value());
  const auto* block1 = state->block_at_height(1);
  CHECK(block1 != nullptr);
  OutPoint coinbase_out;
  coinbase_out.txid = block1->transactions.front().txid();
  coinbase_out.index = 0;
  return spend_tx(coinbase_out, config.block_subsidy - 100);
}

}  // namespace

TEST_CASE("peer state rejects policy-violating transaction without mutating mempool") {
  NodeConfig config = stage3_config();
  config.min_relay_feerate = 100;
  config.mine_blocks = 1;

  auto state = PeerState::from_config(config);
  CHECK(state.has_value());

  const Transaction spend = coinbase_spend_after_block1(config);
  auto rejected = state->accept_transaction(spend);
  CHECK(!rejected.has_value());
  CHECK(rejected.error().code == ErrorCode::kPolicyRejected);
  CHECK_EQ(state->mempool_size(), static_cast<std::size_t>(0));
}

TEST_CASE("simulator block production is deterministic for the same tx sequence") {
  const NodeConfig config = stage3_config();

  const auto run_once = [&]() -> Hash256 {
    const Transaction spend = coinbase_spend_after_block1(config);

    SimulatorOptions options;
    options.steps.push_back(SimulatorStep{.submit_txs = {}, .mine_blocks = 1});
    options.steps.push_back(SimulatorStep{.submit_txs = {spend}, .mine_blocks = 1});

    NodeConfig run = config;
    run.mine_blocks = 0;
    auto summary = run_simulator(run, options);
    if (!summary) {
      return {};
    }
    return summary->tip_hash;
  };

  const Hash256 first = run_once();
  const Hash256 second = run_once();
  CHECK(first != Hash256{});
  CHECK(first == second);
}

TEST_CASE("same mempool admission order yields identical block templates") {
  UtxoSet utxos;
  Coin coin;
  coin.output.value = 1000;
  (void)utxos.add(make_outpoint(0x30, 0), coin);
  (void)utxos.add(make_outpoint(0x31, 0), coin);

  const Transaction low = spend_tx(make_outpoint(0x30, 0), 990);
  const Transaction high = spend_tx(make_outpoint(0x31, 0), 100);

  const BlockTemplateParams params{
      .version = 1, .max_block_size_bytes = 1U << 20U, .max_transactions = 100};

  const auto build_tip_hash = [&]() -> Hash256 {
    Mempool pool(MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U});
    CHECK(pool.accept(low, utxos).has_value());
    CHECK(pool.accept(high, utxos).has_value());

    blockchain::protocol::BlockHeader tip;
    auto tmpl = build_block_template(tip, 2000, pool, utxos, params);
    CHECK(tmpl.has_value());
    return tmpl->block.header.hash();
  };

  CHECK(build_tip_hash() == build_tip_hash());
}
