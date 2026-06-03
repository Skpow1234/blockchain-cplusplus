#include <cstdio>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/storage/chain_store.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::consensus::ConsensusParams;
using blockchain::crypto::zero_hash;
using blockchain::node::build_genesis_block;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::protocol::Block;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::kBlockVersion;
using blockchain::storage::ChainStore;

namespace {

std::string temp_data_dir() {
  static int counter = 0;
  const std::string dir = "test_adv_storage_" + std::to_string(++counter);
  (void)std::remove(dir.c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif
  return dir;
}

bool is_regular_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return false;
  }
  return input.tellg() > 0;
}

void write_bytes(const std::string& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST_CASE("decode rejects wrong ledger magic") {
  NodeConfig config;
  const Block genesis = build_genesis_block(config);
  auto encoded = ChainStore::encode_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{});
  CHECK(encoded.has_value());
  (*encoded)[0] = std::byte{0x00};

  auto err =
      ChainStore::decode_ledger(std::span<const std::byte>(encoded->data(), encoded->size()));
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}

TEST_CASE("decode rejects unsupported ledger format version") {
  NodeConfig config;
  const Block genesis = build_genesis_block(config);
  auto encoded = ChainStore::encode_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{});
  CHECK(encoded.has_value());

  auto err =
      ChainStore::decode_ledger(std::span<const std::byte>(encoded->data(), encoded->size()));
  CHECK(err.has_value());

  (*encoded)[4] = std::byte{0x02};
  err = ChainStore::decode_ledger(std::span<const std::byte>(encoded->data(), encoded->size()));
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}

TEST_CASE("save rejects non-contiguous block heights") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  Block genesis = build_genesis_block(config);
  Block bad = genesis;
  bad.header.height = 2;
  bad.header.prev_block = genesis.header.hash();

  ChainStore store(dir);
  const Block blocks[] = {genesis, bad};
  auto err = store.save_ledger(blocks, ConsensusParams{});
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}

TEST_CASE("load rejects ledger whose blocks fail replay validation") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  config.genesis_timestamp = 88;
  const Block genesis = build_genesis_block(config);

  Block bad;
  bad.header.version = kBlockVersion;
  bad.header.height = 1;
  bad.header.timestamp = config.genesis_timestamp + 1;
  bad.header.prev_block = zero_hash();
  bad.header.merkle_root = compute_merkle_root(bad.transactions);

  const std::vector<Block> blocks = {genesis, bad};
  ChainStore store(dir);
  CHECK(store.save_ledger(blocks, ConsensusParams{}).has_value());

  auto err = store.load_chain();
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("load rejects checksum tampering") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  const Block genesis = build_genesis_block(config);

  ChainStore store(dir);
  CHECK(store.save_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{}).has_value());

  auto bytes = ChainStore::encode_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{});
  CHECK(bytes.has_value());
  bytes->back() = std::byte{0x00};
  write_bytes(ChainStore::ledger_path(dir), *bytes);

  auto err = store.load_chain();
  CHECK(!err.has_value());
  CHECK(err.error().code == ErrorCode::kStorageCorruption);
}

TEST_CASE("peer state restore fails when ledger is missing") {
  const std::string dir = temp_data_dir();
  ChainStore store(dir);
  CHECK(!store.ledger_exists());

  NodeConfig config;
  config.data_dir = dir;
  config.restore = true;

  auto state = PeerState::from_config(config);
  CHECK(!state.has_value());
  CHECK(state.error().code == ErrorCode::kInvalidConfig);
}

TEST_CASE("incomplete temp ledger is not loaded as canonical state") {
  const std::string dir = temp_data_dir();
  NodeConfig config;
  const Block genesis = build_genesis_block(config);

  ChainStore store(dir);
  auto encoded = ChainStore::encode_ledger(std::span<const Block>(&genesis, 1), ConsensusParams{});
  CHECK(encoded.has_value());

  write_bytes(ChainStore::ledger_temp_path(dir),
              std::span<const std::byte>(encoded->data(), encoded->size() / 2));

  CHECK(!store.ledger_exists());
  CHECK(!is_regular_file(ChainStore::ledger_path(dir)));
  auto err = store.load_chain();
  CHECK(!err.has_value());
}
