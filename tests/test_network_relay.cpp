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
#include "blockchain/node/simulator.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::crypto::Hash256;
using blockchain::crypto::to_hex;
using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::node::RelayClientOptions;
using blockchain::node::RelayServerOptions;
using blockchain::node::run_relay_client;
using blockchain::node::run_relay_server;
using blockchain::node::run_simulator;
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
  client_config.persist = false;
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
  client_config.persist = false;
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
  client_config.persist = false;
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

TEST_CASE("relay server restores from disk and serves blocks to a peer") {
  blockchain::net::SocketLibrary lib;

  const std::string dir = "test_relay_restore_srv";
  (void)std::remove((dir + "/ledger.bin").c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif

  NodeConfig seed = relay_test_config();
  seed.data_dir = dir;
  seed.mine_blocks = 2;
  seed.persist = true;
  CHECK(run_simulator(seed).has_value());

  NodeConfig restored = seed;
  restored.restore = true;
  restored.mine_blocks = 0;
  restored.persist = false;
  auto restored_state = PeerState::from_config(restored);
  CHECK(restored_state.has_value());
  CHECK_EQ(restored_state->height(), static_cast<std::uint32_t>(2));

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config = seed;
  server_config.restore = true;
  server_config.mine_blocks = 0;
  server_config.mine_after_tx = 0;
  server_config.persist = false;
  server_config.network_mode = NetworkMode::kRelay;

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
    if (!client || !serve_relay_connection(*client, server_config).has_value()) {
      server_failed.store(true);
    }
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config = seed;
  client_config.restore = false;
  client_config.persist = false;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  auto result = run_relay_client(client_config);
  CHECK(result.has_value());
  CHECK_EQ(result->height, static_cast<std::uint32_t>(2));

  server.join();
  CHECK(!server_failed.load());
}

TEST_CASE("relay server serves two sequential peer connections") {
  blockchain::net::SocketLibrary lib;

  auto listener = blockchain::net::TcpListener::bind(
      blockchain::net::TcpEndpoint{.host = "127.0.0.1", .port = 0});
  CHECK(listener.has_value());
  auto bound_port = listener->bound_port();
  CHECK(bound_port.has_value());
  listener->close();

  std::atomic<bool> server_failed{true};

  NodeConfig server_config = relay_test_config();
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 1;
  server_config.listen_port = *bound_port;
  server_config.relay_max_sessions = 2;

  std::thread server([&]() {
    RelayServerOptions options{.max_sessions = 2};
    auto result = run_relay_server(server_config, options);
    if (result && result->sessions_completed == 2) {
      server_failed.store(false);
    }
  });

  NodeConfig client_config = server_config;
  client_config.mine_blocks = 0;
  client_config.persist = false;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = *bound_port;

  auto first = run_relay_client(client_config);
  CHECK(first.has_value());
  CHECK_EQ(first->height, static_cast<std::uint32_t>(1));

  auto second = run_relay_client(client_config);
  CHECK(second.has_value());
  CHECK_EQ(second->height, static_cast<std::uint32_t>(1));
  CHECK(first->tip_hash == second->tip_hash);

  server.join();
  CHECK(!server_failed.load());
}

TEST_CASE("relay server shares chain state across sequential sessions") {
  blockchain::net::SocketLibrary lib;

  auto listener = blockchain::net::TcpListener::bind(
      blockchain::net::TcpEndpoint{.host = "127.0.0.1", .port = 0});
  CHECK(listener.has_value());
  auto bound_port = listener->bound_port();
  CHECK(bound_port.has_value());
  listener->close();

  std::atomic<bool> server_failed{true};

  NodeConfig server_config = relay_test_config();
  server_config.network_mode = NetworkMode::kRelay;
  server_config.mine_blocks = 1;
  server_config.mine_after_tx = 1;
  server_config.listen_port = *bound_port;
  server_config.relay_max_sessions = 2;

  std::thread server([&]() {
    RelayServerOptions options{.max_sessions = 2};
    auto result = run_relay_server(server_config, options);
    if (result && result->sessions_completed == 2 &&
        result->last_session.height == static_cast<std::uint32_t>(2)) {
      server_failed.store(false);
    }
  });

  NodeConfig client_config = server_config;
  client_config.mine_blocks = 0;
  client_config.mine_after_tx = 0;
  client_config.persist = false;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = *bound_port;

  const Transaction spend = coinbase_spend_after_block1(server_config);
  RelayClientOptions mine_options;
  mine_options.txs_after_sync.push_back(spend);
  mine_options.blocks_after_tx = 1;

  auto first = run_relay_client(client_config, mine_options);
  CHECK(first.has_value());
  CHECK_EQ(first->height, static_cast<std::uint32_t>(2));

  auto second = run_relay_client(client_config);
  CHECK(second.has_value());
  CHECK_EQ(second->height, static_cast<std::uint32_t>(2));
  CHECK(first->tip_hash == second->tip_hash);

  server.join();
  CHECK(!server_failed.load());
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
