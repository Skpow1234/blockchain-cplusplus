#include <cstdio>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "blockchain/consensus/chain.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/node/simulator.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::consensus::Chain;
using blockchain::consensus::ConsensusParams;
using blockchain::crypto::Hash256;
using blockchain::crypto::to_hex;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::node::build_genesis_block;
using blockchain::node::NodeConfig;
using blockchain::node::run_simulator;
using blockchain::node::SimulatorOptions;
using blockchain::node::SimulatorStep;
using blockchain::production::BlockTemplateParams;
using blockchain::production::build_block_template;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;

namespace {

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

NodeConfig base_config() {
  NodeConfig config;
  config.genesis_timestamp = 1'700'000'000;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex = to_hex(tag_hash(0xA1));
  return config;
}

ConsensusParams consensus_from(const NodeConfig& config) {
  ConsensusParams params;
  if (config.block_subsidy != 0) {
    params.block_subsidy = config.block_subsidy;
  }
  if (config.coinbase_maturity != 0) {
    params.coinbase_maturity = config.coinbase_maturity;
  }
  return params;
}

BlockTemplateParams template_params(const NodeConfig& config, const ConsensusParams& consensus) {
  BlockTemplateParams params;
  params.version = blockchain::protocol::kBlockVersion;
  params.max_block_size_bytes = blockchain::protocol::kMaxBlockSizeBytes;
  params.max_transactions = blockchain::protocol::kMaxTransactionsPerBlock;
  params.consensus = consensus;
  if (config.coinbase_recipient_hex.empty()) {
    params.coinbase_recipient = blockchain::crypto::zero_hash();
  } else {
    params.coinbase_recipient = *blockchain::crypto::hash_from_hex(config.coinbase_recipient_hex);
  }
  return params;
}

Transaction make_spend(const OutPoint& prevout, std::uint64_t input_value, std::uint64_t fee,
                       const Hash256& to) {
  Transaction tx;
  TxInput input;
  input.prevout = prevout;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = input_value - fee;
  output.recipient = to;
  tx.outputs.push_back(output);
  return tx;
}

OutPoint coinbase_out_after_block1(const NodeConfig& config) {
  const ConsensusParams consensus = consensus_from(config);
  const auto genesis = build_genesis_block(config);
  auto chain = Chain::create(genesis, consensus);
  CHECK(chain.has_value());
  Mempool pool(MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U});
  auto tmpl = build_block_template(chain->tip(), config.genesis_timestamp + 1, pool, chain->utxos(),
                                   template_params(config, consensus));
  CHECK(tmpl.has_value());
  OutPoint out;
  out.txid = tmpl->block.transactions.front().txid();
  out.index = 0;
  return out;
}

std::string temp_data_dir() {
  static int counter = 0;
  const std::string dir = "test_simulator_" + std::to_string(++counter);
  (void)std::remove((dir + "/ledger.bin").c_str());
  (void)std::remove((dir + "/ledger.bin.tmp").c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif
  return dir;
}

}  // namespace

TEST_CASE("run_simulator mines blocks and grows the utxo set") {
  NodeConfig config = base_config();
  config.mine_blocks = 2;

  auto summary = run_simulator(config);
  CHECK(summary.has_value());
  CHECK_EQ(summary->height, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->blocks_mined, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->utxo_count, static_cast<std::size_t>(2));
  CHECK_EQ(summary->txs_admitted, static_cast<std::uint32_t>(0));
  CHECK_EQ(summary->txs_included, static_cast<std::uint32_t>(0));
  CHECK(!summary->restored_from_disk);
  CHECK(summary->genesis_hash != summary->tip_hash);
}

TEST_CASE("run_simulator with zero blocks leaves only genesis height") {
  NodeConfig config = base_config();

  auto summary = run_simulator(config);
  CHECK(summary.has_value());
  CHECK_EQ(summary->height, static_cast<std::uint32_t>(0));
  CHECK_EQ(summary->blocks_mined, static_cast<std::uint32_t>(0));
  CHECK_EQ(summary->utxo_count, static_cast<std::size_t>(0));
}

TEST_CASE("run_simulator scenario mines spends and collects fees") {
  NodeConfig config = base_config();
  const Hash256 payee = tag_hash(0xB2);
  const OutPoint coinbase_out = coinbase_out_after_block1(config);
  const Transaction spend = make_spend(coinbase_out, config.block_subsidy, /*fee=*/100, payee);

  SimulatorOptions options;
  options.steps.push_back(SimulatorStep{.submit_txs = {}, .mine_blocks = 1});
  options.steps.push_back(SimulatorStep{.submit_txs = {spend}, .mine_blocks = 1});

  auto summary = run_simulator(config, options);
  CHECK(summary.has_value());
  CHECK_EQ(summary->height, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->blocks_mined, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->txs_admitted, static_cast<std::uint32_t>(1));
  CHECK_EQ(summary->txs_included, static_cast<std::uint32_t>(1));
  CHECK_EQ(summary->total_fees, static_cast<std::uint64_t>(100));
  CHECK_EQ(summary->utxo_count, static_cast<std::size_t>(2));
}

TEST_CASE("run_simulator is deterministic for the same configuration") {
  NodeConfig config = base_config();
  config.mine_blocks = 3;

  const auto first = run_simulator(config);
  const auto second = run_simulator(config);
  CHECK(first.has_value());
  CHECK(second.has_value());
  CHECK(first->tip_hash == second->tip_hash);
  CHECK_EQ(first->height, second->height);
}

TEST_CASE("run_simulator persist then restore matches mined chain") {
  const std::string dir = temp_data_dir();

  NodeConfig mine_config = base_config();
  mine_config.data_dir = dir;
  mine_config.mine_blocks = 2;
  mine_config.persist = true;

  auto mined = run_simulator(mine_config);
  CHECK(mined.has_value());
  CHECK(!mined->restored_from_disk);
  CHECK_EQ(mined->height, static_cast<std::uint32_t>(2));

  NodeConfig restore_config = mine_config;
  restore_config.restore = true;
  restore_config.mine_blocks = 0;
  restore_config.persist = false;

  auto restored = run_simulator(restore_config);
  CHECK(restored.has_value());
  CHECK(restored->restored_from_disk);
  CHECK_EQ(restored->height, mined->height);
  CHECK(restored->tip_hash == mined->tip_hash);
  CHECK_EQ(restored->utxo_count, mined->utxo_count);
}

TEST_CASE("run_simulator restore then mines additional blocks") {
  const std::string dir = temp_data_dir();

  NodeConfig seed = base_config();
  seed.data_dir = dir;
  seed.mine_blocks = 1;
  seed.persist = true;
  CHECK(run_simulator(seed).has_value());

  NodeConfig resume = seed;
  resume.restore = true;
  resume.mine_blocks = 2;
  resume.persist = true;

  auto resumed = run_simulator(resume);
  CHECK(resumed.has_value());
  CHECK(resumed->restored_from_disk);
  CHECK_EQ(resumed->blocks_mined, static_cast<std::uint32_t>(2));
  CHECK_EQ(resumed->height, static_cast<std::uint32_t>(3));

  NodeConfig verify = seed;
  verify.restore = true;
  verify.mine_blocks = 0;
  verify.persist = false;
  auto final_state = run_simulator(verify);
  CHECK(final_state.has_value());
  CHECK(final_state->tip_hash == resumed->tip_hash);
}

TEST_CASE("run_simulator restore rejects genesis mismatch") {
  const std::string dir = temp_data_dir();

  NodeConfig mine_config = base_config();
  mine_config.data_dir = dir;
  mine_config.mine_blocks = 1;
  mine_config.persist = true;
  CHECK(run_simulator(mine_config).has_value());

  NodeConfig bad_restore = mine_config;
  bad_restore.genesis_timestamp = mine_config.genesis_timestamp + 1;
  bad_restore.restore = true;
  bad_restore.mine_blocks = 0;
  bad_restore.persist = false;

  auto restored = run_simulator(bad_restore);
  CHECK(!restored.has_value());
}

TEST_CASE("run_simulator restore rejects consensus parameter mismatch") {
  const std::string dir = temp_data_dir();

  NodeConfig mine_config = base_config();
  mine_config.data_dir = dir;
  mine_config.mine_blocks = 1;
  mine_config.persist = true;
  CHECK(run_simulator(mine_config).has_value());

  NodeConfig bad_restore = mine_config;
  bad_restore.block_subsidy = mine_config.block_subsidy + 1;
  bad_restore.restore = true;
  bad_restore.mine_blocks = 0;
  bad_restore.persist = false;

  auto restored = run_simulator(bad_restore);
  CHECK(!restored.has_value());
}
