#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/network.hpp"
#include "testing.hpp"

using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::run_relay_client;
using blockchain::node::serve_relay_connection;

TEST_CASE("relay client syncs a mined block from the server") {
  blockchain::net::SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config;
  server_config.node_id = "relay-server";
  server_config.network_mode = NetworkMode::kRelay;
  server_config.genesis_timestamp = 1'700'000'000;
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
    if (!serve_relay_connection(*client, server_config)) {
      server_failed.store(true);
    }
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config;
  client_config.node_id = "relay-client";
  client_config.network_mode = NetworkMode::kRelay;
  client_config.genesis_timestamp = server_config.genesis_timestamp;
  client_config.mine_blocks = 0;
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();

  CHECK(run_relay_client(client_config).has_value());

  server.join();
  CHECK(!server_failed.load());
}
