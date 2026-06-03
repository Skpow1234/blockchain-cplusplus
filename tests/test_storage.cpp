#include <cstdio>
#include <fstream>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <span>
#include <string>
#include <vector>

#include "blockchain/consensus/chain.hpp"
#include "blockchain/consensus/params.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/node/simulator.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/storage/chain_store.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::consensus::Chain;
using blockchain::consensus::ConsensusParams;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::node::NodeConfig;
using blockchain::node::build_genesis_block;
using blockchain::node::run_simulator;
using blockchain::production::BlockTemplateParams;
using blockchain::production::build_block_template;
using blockchain::protocol::Block;
using blockchain::storage::ChainStore;

namespace {

std::string temp_data_dir() {
  static int counter = 0;
  const std::string dir = "test_storage_" + std::to_string(++counter);
  (void)std::remove(dir.c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif
  return dir;
}

BlockTemplateParams template_params(const ConsensusParams& consensus) {
  BlockTemplateParams params;
  params.version = blockchain::protocol::kBlockVersion;
  params.max_block_size_bytes = blockchain::protocol::kMaxBlockSizeBytes;
  params.max_transactions = blockchain::protocol::kMaxTransactionsPerBlock;
  params.consensus = consensus;
  params.coinbase_recipient = blockchain::crypto::zero_hash();
  return params;
}

}  // namespace

TEST_CASE("ledger encode and decode round-trip") {
  const ConsensusParams consensus{.block_subsidy = 500, .coinbase_maturity = 1};
  NodeConfig config;
  config.genesis_timestamp = 99;

  const Block genesis = build_genesis_block(config);
  auto chain = Chain::create(genesis, consensus);
  CHECK(chain.has_value());

  Mempool pool(MempoolLimits{.max_transactions = 10, .max_total_bytes = 1U << 20U});
  auto tmpl = build_block_template(chain->tip(), 100, pool, chain->utxos(), template_params(consensus));
  CHECK(tmpl.has_value());
  CHECK(chain->submit_block(tmpl->block).has_value());

  const std::vector<Block> blocks = {genesis, tmpl->block};
  auto encoded = ChainStore::encode_ledger(blocks, consensus);
  CHECK(encoded.has_value());

  auto decoded = ChainStore::decode_ledger(
      std::span<const std::byte>(encoded->data(), encoded->size()));
  CHECK(decoded.has_value());
  CHECK_EQ(decoded->first.size(), static_cast<std::size_t>(2));
  CHECK_EQ(decoded->second.block_subsidy, static_cast<std::uint64_t>(500));
}

TEST_CASE("chain store save and load replays validation") {
  const std::string dir = temp_data_dir();
  const ConsensusParams consensus{.block_subsidy = 1'000, .coinbase_maturity = 2};
  NodeConfig config;
  config.genesis_timestamp = 500;
  config.data_dir = dir;

  const Block genesis = build_genesis_block(config);
  auto chain = Chain::create(genesis, consensus);
  CHECK(chain.has_value());

  Mempool pool(MempoolLimits{.max_transactions = 10, .max_total_bytes = 1U << 20U});
  std::vector<Block> blocks = {genesis};
  for (std::uint32_t height = 1; height <= 3; ++height) {
    auto tmpl = build_block_template(chain->tip(), config.genesis_timestamp + height, pool,
                                     chain->utxos(), template_params(consensus));
    CHECK(tmpl.has_value());
    CHECK(chain->submit_block(tmpl->block).has_value());
    blocks.push_back(tmpl->block);
  }

  ChainStore store(dir);
  CHECK(store.save_ledger(blocks, consensus).has_value());

  auto restored = store.load_chain();
  CHECK(restored.has_value());
  CHECK_EQ(restored->height(), static_cast<std::uint32_t>(3));
  CHECK(restored->tip_hash() == chain->tip_hash());
  CHECK_EQ(restored->utxos().size(), chain->utxos().size());
}

TEST_CASE("simulator persist then restore matches mined chain") {
  const std::string dir = temp_data_dir();

  NodeConfig mine_config;
  mine_config.data_dir = dir;
  mine_config.genesis_timestamp = 2'000;
  mine_config.mine_blocks = 2;
  mine_config.block_subsidy = 750;
  mine_config.coinbase_maturity = 5;
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

TEST_CASE("corrupted ledger checksum is rejected") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  config.genesis_timestamp = 1;
  const Block genesis = build_genesis_block(config);
  const ConsensusParams consensus{};

  ChainStore store(dir);
  CHECK(store.save_ledger(std::span<const Block>(&genesis, 1), consensus).has_value());

  auto bytes = ChainStore::encode_ledger(std::span<const Block>(&genesis, 1), consensus);
  CHECK(bytes.has_value());
  bytes->back() = std::byte{0x00};

  std::ofstream out(ChainStore::ledger_path(dir), std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes->data()),
            static_cast<std::streamsize>(bytes->size()));

  auto err = store.load_chain();
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}

TEST_CASE("truncated ledger file is rejected") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  const Block genesis = build_genesis_block(config);

  ChainStore store(dir);
  CHECK(store.save_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{}).has_value());

  std::vector<std::byte> truncated = {std::byte{0x01}, std::byte{0x02}};
  std::ofstream out(ChainStore::ledger_path(dir), std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(truncated.data()),
            static_cast<std::streamsize>(truncated.size()));

  auto err = store.load_chain();
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}
