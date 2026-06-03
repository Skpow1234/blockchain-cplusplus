// Integration: simulator persistence, relay restore, and client catch-up.
#include <atomic>
#include <chrono>
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
#include "blockchain/storage/chain_store.hpp"
#include "testing.hpp"

using blockchain::crypto::Hash256;
using blockchain::crypto::to_hex;
using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::node::RelayServerOptions;
using blockchain::node::run_relay_client;
using blockchain::node::run_relay_server;
using blockchain::node::run_simulator;
using blockchain::storage::ChainStore;

namespace {

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

NodeConfig integration_config() {
  NodeConfig config;
  config.genesis_timestamp = 1'700'000'100;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex = to_hex(tag_hash(0xC3));
  return config;
}

std::string fresh_data_dir(const char* prefix) {
  static int counter = 0;
  const std::string dir = std::string(prefix) + std::to_string(++counter);
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

TEST_CASE("simulator persist then relay restore serves chain to client") {
  blockchain::net::SocketLibrary lib;

  const std::string dir = fresh_data_dir("test_restart_sim_relay_");

  NodeConfig seed = integration_config();
  seed.data_dir = dir;
  seed.mine_blocks = 2;
  seed.persist = true;

  auto mined = run_simulator(seed);
  CHECK(mined.has_value());
  CHECK(ChainStore(dir).ledger_exists());

  auto listener = blockchain::net::TcpListener::bind(blockchain::net::TcpEndpoint{
      .host = "127.0.0.1", .port = 0});
  CHECK(listener.has_value());
  auto bound_port = listener->bound_port();
  CHECK(bound_port.has_value());
  listener->close();

  std::atomic<bool> server_failed{true};

  NodeConfig server = seed;
  server.network_mode = NetworkMode::kRelay;
  server.restore = true;
  server.mine_blocks = 0;
  server.persist = false;
  server.listen_port = *bound_port;

  std::thread server_thread([&]() {
    auto result = run_relay_server(server);
    if (result && result->last_session.height == mined->height) {
      server_failed.store(false);
    }
  });

  NodeConfig client = seed;
  client.network_mode = NetworkMode::kRelay;
  client.mine_blocks = 0;
  client.restore = false;
  client.persist = false;
  client.peer_host = "127.0.0.1";
  client.peer_port = *bound_port;

  auto synced = run_relay_client(client);
  CHECK(synced.has_value());
  CHECK_EQ(synced->height, mined->height);
  CHECK(synced->tip_hash == mined->tip_hash);

  server_thread.join();
  CHECK(!server_failed.load());
}

TEST_CASE("relay session persist then peer state restore matches tip") {
  blockchain::net::SocketLibrary lib;

  const std::string dir = fresh_data_dir("test_restart_relay_persist_");

  NodeConfig seed = integration_config();
  seed.data_dir = dir;
  seed.mine_blocks = 2;
  seed.persist = true;

  CHECK(run_simulator(seed).has_value());

  auto listener = blockchain::net::TcpListener::bind(blockchain::net::TcpEndpoint{
      .host = "127.0.0.1", .port = 0});
  CHECK(listener.has_value());
  const std::uint16_t port = *listener->bound_port();
  listener->close();

  NodeConfig server = seed;
  server.network_mode = NetworkMode::kRelay;
  server.restore = true;
  server.mine_blocks = 0;
  server.persist = true;
  server.listen_port = port;

  std::atomic<bool> server_failed{true};
  std::thread server_thread([&]() {
    if (run_relay_server(server).has_value()) {
      server_failed.store(false);
    }
  });

  NodeConfig client = seed;
  client.network_mode = NetworkMode::kRelay;
  client.mine_blocks = 0;
  client.persist = false;
  client.peer_host = "127.0.0.1";
  client.peer_port = port;

  CHECK(run_relay_client(client).has_value());

  server_thread.join();
  CHECK(!server_failed.load());
  CHECK(ChainStore(dir).ledger_exists());

  NodeConfig reload = seed;
  reload.restore = true;
  reload.mine_blocks = 0;
  reload.persist = false;

  auto state = PeerState::from_config(reload);
  CHECK(state.has_value());
  CHECK_EQ(state->height(), static_cast<std::uint32_t>(2));

  auto reloaded_chain = ChainStore(dir).load_chain();
  CHECK(reloaded_chain.has_value());
  CHECK(reloaded_chain->tip_hash() == state->tip_hash());
}
