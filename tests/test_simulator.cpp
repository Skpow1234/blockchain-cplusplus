#include "blockchain/node/config.hpp"
#include "blockchain/node/simulator.hpp"
#include "testing.hpp"

using blockchain::node::NodeConfig;
using blockchain::node::run_simulator;

TEST_CASE("run_simulator mines blocks and grows the utxo set") {
  NodeConfig config;
  config.genesis_timestamp = 1'000;
  config.mine_blocks = 2;
  config.block_subsidy = 500;
  config.coinbase_maturity = 100;

  auto summary = run_simulator(config);
  CHECK(summary.has_value());
  CHECK_EQ(summary->height, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->blocks_mined, static_cast<std::uint32_t>(2));
  CHECK_EQ(summary->utxo_count, static_cast<std::size_t>(2));
  CHECK(summary->genesis_hash != summary->tip_hash);
}

TEST_CASE("run_simulator with zero blocks leaves only genesis height") {
  NodeConfig config;
  config.genesis_timestamp = 42;

  auto summary = run_simulator(config);
  CHECK(summary.has_value());
  CHECK_EQ(summary->height, static_cast<std::uint32_t>(0));
  CHECK_EQ(summary->blocks_mined, static_cast<std::uint32_t>(0));
  CHECK_EQ(summary->utxo_count, static_cast<std::size_t>(0));
}
