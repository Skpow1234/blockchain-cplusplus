#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/network.hpp"
#include "testing.hpp"

using blockchain::net::SocketLibrary;
using blockchain::net::TcpEndpoint;
using blockchain::net::TcpListener;
using blockchain::node::NodeConfig;
using blockchain::node::run_ping_client;
using blockchain::node::serve_ping_connection;

namespace {

}  // namespace

TEST_CASE("localhost ping server and client exchange handshake and pong") {
  SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_failed{false};

  NodeConfig server_config;
  server_config.node_id = "server";
  server_config.genesis_timestamp = 42;

  std::thread server([&]() {
    TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = TcpListener::bind(bind_ep);
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
    if (!client || !serve_ping_connection(*client, server_config)) {
      server_failed.store(true);
    }
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  NodeConfig client_config;
  client_config.node_id = "client";
  client_config.peer_host = "127.0.0.1";
  client_config.peer_port = port.load();
  client_config.genesis_timestamp = 42;

  CHECK(run_ping_client(client_config).has_value());

  server.join();
  CHECK(!server_failed.load());
}

TEST_CASE("framed send and recv round-trip bytes") {
  SocketLibrary lib;

  TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
  auto listener = TcpListener::bind(bind_ep);
  CHECK(listener.has_value());
  auto port = listener->bound_port();
  CHECK(port.has_value());

  std::atomic<bool> done{false};
  const std::vector<std::byte> payload = {std::byte{1}, std::byte{2}, std::byte{3}};

  std::thread server([&]() {
    auto client = listener->accept();
    if (client) {
      auto got = client->recv_framed();
      if (got && *got == payload) {
        (void)client->send_framed(*got);
      }
    }
    done.store(true);
  });

  TcpEndpoint connect_ep{.host = "127.0.0.1", .port = *port};
  auto socket = blockchain::net::TcpSocket::connect(connect_ep);
  CHECK(socket.has_value());
  CHECK(socket->send_framed(payload).has_value());
  auto echo = socket->recv_framed();
  CHECK(echo.has_value());
  CHECK(*echo == payload);

  server.join();
  CHECK(done.load());
}
