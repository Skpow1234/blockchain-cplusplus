#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

#include "blockchain/crypto/hash.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/network.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::crypto::Hash256;
using blockchain::crypto::to_hex;
using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::node::RelayClientOptions;
using blockchain::node::run_relay_client;
using blockchain::node::serve_relay_connection;
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

NodeConfig relay_test_config() {
  NodeConfig config;
  config.genesis_timestamp = 1'700'000'000;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex = to_hex(tag_hash(0xA1));
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
  return make_spend(coinbase_out, config.block_subsidy, 100, tag_hash(0xB2));
}

}  // namespace

TEST_CASE("relay client syncs a mined block from the server") {
  blockchain::net::SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config = relay_test_config();
  server_config.node_id = "relay-server";
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 1;

  std::thread server([&]() {
    blockchain::net::TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = blockchain::net::TcpListener::bind(bind_ep);
    if (!listener) {
      server_failed.store(true);
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      server_failed.store(true);
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      server_failed.store(true);
      return;
    }
    if (!serve_relay_connection(*client, server_config).has_value()) {
      server_failed.store(true);
    }
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config = server_config;
  client_config.node_id = "relay-client";
  client_config.network_mode = NetworkMode::kRelay;
  client_config.mine_blocks = 0;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  auto result = run_relay_client(client_config);
  CHECK(result.has_value());
  CHECK_EQ(result->height, static_cast<std::uint32_t>(1));

  server.join();
  CHECK(!server_failed.load());
}

TEST_CASE("relay client catches up across multiple mined blocks") {
  blockchain::net::SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config = relay_test_config();
  server_config.node_id = "relay-server";
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 3;

  std::thread server([&]() {
    blockchain::net::TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = blockchain::net::TcpListener::bind(bind_ep);
    if (!listener) {
      server_failed.store(true);
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      server_failed.store(true);
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      server_failed.store(true);
      return;
    }
    if (!serve_relay_connection(*client, server_config).has_value()) {
      server_failed.store(true);
    }
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config = server_config;
  client_config.node_id = "relay-client";
  client_config.mine_blocks = 0;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  auto result = run_relay_client(client_config);
  CHECK(result.has_value());
  CHECK_EQ(result->height, static_cast<std::uint32_t>(3));

  auto expected = PeerState::from_config(server_config);
  CHECK(expected.has_value());
  CHECK(result->tip_hash == expected->tip_hash());

  server.join();
  CHECK(!server_failed.load());
}

TEST_CASE("relay client tx announce is admitted on the server") {
  blockchain::net::SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<std::size_t> server_mempool_size{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config = relay_test_config();
  server_config.node_id = "relay-server";
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 1;

  std::thread server([&]() {
    blockchain::net::TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = blockchain::net::TcpListener::bind(bind_ep);
    if (!listener) {
      server_failed.store(true);
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      server_failed.store(true);
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      server_failed.store(true);
      return;
    }
    auto summary = serve_relay_connection(*client, server_config);
    if (!summary) {
      server_failed.store(true);
      return;
    }
    server_mempool_size.store(summary->mempool_size);
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config = server_config;
  client_config.node_id = "relay-client";
  client_config.mine_blocks = 0;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  const Transaction spend = coinbase_spend_after_block1(server_config);
  RelayClientOptions options;
  options.txs_after_sync.push_back(spend);

  auto result = run_relay_client(client_config, options);
  CHECK(result.has_value());
  CHECK_EQ(result->height, static_cast<std::uint32_t>(1));

  server.join();
  CHECK(!server_failed.load());
  CHECK_EQ(server_mempool_size.load(), static_cast<std::size_t>(1));
}

TEST_CASE("relay server mines announced tx and client syncs the new block") {
  blockchain::net::SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<std::uint32_t> server_height{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config = relay_test_config();
  server_config.node_id = "relay-server";
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 1;
  server_config.mine_after_tx = 1;

  std::thread server([&]() {
    blockchain::net::TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = blockchain::net::TcpListener::bind(bind_ep);
    if (!listener) {
      server_failed.store(true);
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      server_failed.store(true);
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      server_failed.store(true);
      return;
    }
    auto summary = serve_relay_connection(*client, server_config);
    if (!summary) {
      server_failed.store(true);
      return;
    }
    server_height.store(summary->height);
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config = server_config;
  client_config.node_id = "relay-client";
  client_config.mine_blocks = 0;
  client_config.mine_after_tx = 0;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  const Transaction spend = coinbase_spend_after_block1(server_config);
  RelayClientOptions options;
  options.txs_after_sync.push_back(spend);
  options.blocks_after_tx = 1;

  auto result = run_relay_client(client_config, options);
  CHECK(result.has_value());
  CHECK_EQ(result->height, static_cast<std::uint32_t>(2));

  server.join();
  CHECK(!server_failed.load());
  CHECK_EQ(server_height.load(), static_cast<std::uint32_t>(2));
}

TEST_CASE("relay persist and restore reloads the same tip") {
  const std::string dir = "test_relay_persist_1";
  (void)std::remove((dir + "/ledger.bin").c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif

  NodeConfig config = relay_test_config();
  config.data_dir = dir;
  config.mine_blocks = 2;
  config.persist = true;

  auto first = PeerState::from_config(config);
  CHECK(first.has_value());
  const Hash256 tip = first->tip_hash();

  config.restore = true;
  config.mine_blocks = 0;
  config.persist = false;

  auto second = PeerState::from_config(config);
  CHECK(second.has_value());
  CHECK(second->tip_hash() == tip);
  CHECK_EQ(second->height(), static_cast<std::uint32_t>(2));
}
