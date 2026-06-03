#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/network.hpp"
#include "testing.hpp"

using blockchain::net::BlockRequestPayload;
using blockchain::net::HandshakePayload;
using blockchain::net::kNetworkMagic;
using blockchain::net::make_block_request_message;
using blockchain::net::make_handshake_message;
using blockchain::net::P2pMessage;
using blockchain::net::P2pMessageType;
using blockchain::net::parse_reject_message;
using blockchain::net::recv_message;
using blockchain::net::RejectCode;
using blockchain::net::send_message;
using blockchain::net::SocketLibrary;
using blockchain::net::TcpEndpoint;
using blockchain::net::TcpListener;
using blockchain::net::TcpSocket;
using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::serve_relay_connection;
using blockchain::protocol::kProtocolVersion;

namespace {

HandshakePayload sample_handshake() {
  HandshakePayload hs;
  hs.network_magic = kNetworkMagic;
  hs.height = 0;
  hs.tip_hash = blockchain::crypto::zero_hash();
  hs.node_id = "adversary";
  return hs;
}

[[nodiscard]] bool exchange_valid_handshake(TcpSocket& socket) {
  auto outbound = make_handshake_message(sample_handshake());
  if (!outbound) {
    return false;
  }
  if (!send_message(socket, *outbound).has_value()) {
    return false;
  }
  auto inbound = recv_message(socket);
  return inbound.has_value() && inbound->type == P2pMessageType::kHandshake;
}

}  // namespace

TEST_CASE("relay server rejects malformed P2P bytes after handshake") {
  SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_ok{false};

  NodeConfig server_config;
  server_config.network_mode = NetworkMode::kRelay;

  std::thread server([&]() {
    TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = TcpListener::bind(bind_ep);
    if (!listener) {
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      return;
    }
    server_ok.store(serve_relay_connection(*client, server_config).has_value());
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  const std::vector<std::byte> garbage(32, std::byte{0xAB});
  CHECK(socket->send_framed(garbage).has_value());

  server.join();
  CHECK(!server_ok.load());
}

TEST_CASE("relay server rejects invalid P2P checksum on the wire") {
  SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_ok{false};

  NodeConfig server_config;
  server_config.network_mode = NetworkMode::kRelay;

  std::thread server([&]() {
    TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = TcpListener::bind(bind_ep);
    if (!listener) {
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      return;
    }
    server_ok.store(serve_relay_connection(*client, server_config).has_value());
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  P2pMessage ping;
  ping.version = kProtocolVersion;
  ping.type = P2pMessageType::kPing;
  ping.payload = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                  std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  std::vector<std::byte> wire = ping.to_bytes();
  CHECK(wire.size() > 20);
  wire[wire.size() - 1] = std::byte{0x00};
  CHECK(socket->send_framed(wire).has_value());

  server.join();
  CHECK(!server_ok.load());
}

TEST_CASE("relay server rejects empty tx announce payload") {
  SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_ok{false};

  NodeConfig server_config;
  server_config.network_mode = NetworkMode::kRelay;

  std::thread server([&]() {
    TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = TcpListener::bind(bind_ep);
    if (!listener) {
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      return;
    }
    server_ok.store(serve_relay_connection(*client, server_config).has_value());
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  P2pMessage message;
  message.version = kProtocolVersion;
  message.type = P2pMessageType::kTxAnnounce;
  CHECK(send_message(*socket, message).has_value());

  server.join();
  CHECK(!server_ok.load());
}

TEST_CASE("relay server answers unknown block height with reject") {
  SocketLibrary lib;

  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> server_ok{false};

  NodeConfig server_config;
  server_config.network_mode = NetworkMode::kRelay;

  std::thread server([&]() {
    TcpEndpoint bind_ep{.host = "127.0.0.1", .port = 0};
    auto listener = TcpListener::bind(bind_ep);
    if (!listener) {
      return;
    }
    auto bound = listener->bound_port();
    if (!bound) {
      return;
    }
    port.store(*bound);

    auto client = listener->accept();
    if (!client) {
      return;
    }
    server_ok.store(serve_relay_connection(*client, server_config).has_value());
  });

  while (port.load() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  auto request = make_block_request_message(BlockRequestPayload{.height = 99});
  CHECK(request.has_value());
  CHECK(send_message(*socket, *request).has_value());

  auto inbound = recv_message(*socket);
  CHECK(inbound.has_value());
  auto reject = parse_reject_message(*inbound);
  CHECK(reject.has_value());
  CHECK(reject->code == RejectCode::kInvalidMessage);

  socket->close();

  server.join();
  CHECK(server_ok.load());
}
