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
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "testing.hpp"

using blockchain::net::BlockRequestPayload;
using blockchain::net::HandshakePayload;
using blockchain::net::kMaxP2pFrameBytes;
using blockchain::net::kNetworkMagic;
using blockchain::net::make_block_request_message;
using blockchain::net::make_handshake_message;
using blockchain::net::make_ping_message;
using blockchain::net::make_tx_announce_message;
using blockchain::net::P2pMessage;
using blockchain::net::P2pMessageType;
using blockchain::net::parse_pong_message;
using blockchain::net::parse_reject_message;
using blockchain::net::recv_message;
using blockchain::net::RejectCode;
using blockchain::net::send_message;
using blockchain::net::SocketLibrary;
using blockchain::net::TcpEndpoint;
using blockchain::net::TcpListener;
using blockchain::net::TcpSocket;
using blockchain::net::TxAnnouncePayload;
using blockchain::node::NetworkMode;
using blockchain::node::NodeConfig;
using blockchain::node::serve_relay_connection;
using blockchain::protocol::kProtocolVersion;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::serialization::ByteWriter;

namespace {

HandshakePayload sample_handshake() {
  HandshakePayload hs;
  hs.network_magic = kNetworkMagic;
  hs.height = 0;
  hs.tip_hash = blockchain::crypto::zero_hash();
  hs.node_id = "adversary";
  return hs;
}

[[nodiscard]] std::uint32_t wire_checksum(std::span<const std::byte> body) {
  const blockchain::crypto::Hash256 digest = blockchain::crypto::sha256(body);
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(digest[i])) << (8U * i);
  }
  return value;
}

[[nodiscard]] std::vector<std::byte> craft_wire(std::uint32_t version, std::uint16_t type,
                                                std::span<const std::byte> payload) {
  ByteWriter writer;
  writer.put_u32(kNetworkMagic);
  writer.put_u32(version);
  writer.put_u16(type);
  writer.put_u32(static_cast<std::uint32_t>(payload.size()));
  if (!payload.empty()) {
    writer.put_bytes(payload);
  }
  const std::vector<std::byte> body = writer.data();
  writer.put_u32(wire_checksum(std::span<const std::byte>(body.data(), body.size())));
  return writer.data();
}

[[nodiscard]] Transaction invalid_spend_tx() {
  Transaction tx;
  TxInput input;
  input.prevout.txid.fill(std::byte{0xEE});
  input.prevout.index = 0;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = 1;
  tx.outputs.push_back(output);
  return tx;
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

struct RelayServerThread {
  std::atomic<std::uint16_t> port{0};
  std::atomic<bool> completed_ok{false};

  std::thread worker;

  explicit RelayServerThread(const NodeConfig& server_config) {
    worker = std::thread([this, server_config]() {
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
      completed_ok.store(serve_relay_connection(*client, server_config).has_value());
    });
  }

  void wait_for_port() {
    while (port.load() == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  void join() { worker.join(); }

  ~RelayServerThread() {
    if (worker.joinable()) {
      worker.join();
    }
  }
};

[[nodiscard]] NodeConfig relay_server_config() {
  NodeConfig config;
  config.network_mode = NetworkMode::kRelay;
  return config;
}

}  // namespace

TEST_CASE("relay server rejects malformed P2P bytes after handshake") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  const std::vector<std::byte> garbage(32, std::byte{0xAB});
  CHECK(socket->send_framed(garbage).has_value());

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects invalid P2P checksum on the wire") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
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
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects unknown P2P message type on the wire") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  const std::vector<std::byte> wire = craft_wire(kProtocolVersion, 0xFFFF, {});
  CHECK(socket->send_framed(wire).has_value());
  socket->close();

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects unsupported protocol version on the wire") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  const std::vector<std::byte> wire =
      craft_wire(kProtocolVersion + 1, static_cast<std::uint16_t>(P2pMessageType::kPing), {});
  CHECK(socket->send_framed(wire).has_value());
  socket->close();

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects consensus-invalid tx announce") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  TxAnnouncePayload payload;
  payload.tx_bytes = invalid_spend_tx().to_bytes();
  auto announce = make_tx_announce_message(payload);
  CHECK(announce.has_value());
  CHECK(send_message(*socket, *announce).has_value());

  auto inbound = recv_message(*socket);
  CHECK(inbound.has_value());
  auto reject = parse_reject_message(*inbound);
  CHECK(reject.has_value());
  CHECK(reject->code == RejectCode::kInvalidMessage);

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects invalid block announce payload") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  const std::vector<std::byte> garbage(64, std::byte{0xBE});
  P2pMessage message;
  message.version = kProtocolVersion;
  message.type = P2pMessageType::kBlockAnnounce;
  message.payload = garbage;
  CHECK(send_message(*socket, message).has_value());

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server handles duplicate ping messages until disconnect") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  for (int n = 0; n < 2; ++n) {
    auto ping =
        make_ping_message(blockchain::net::PingPayload{.nonce = static_cast<std::uint64_t>(n)});
    CHECK(ping.has_value());
    CHECK(send_message(*socket, *ping).has_value());
    auto inbound = recv_message(*socket);
    CHECK(inbound.has_value());
    auto pong = parse_pong_message(*inbound);
    CHECK(pong.has_value());
    CHECK_EQ(pong->nonce, static_cast<std::uint64_t>(n));
  }

  socket->close();
  server.join();
  CHECK(server.completed_ok.load());
}

TEST_CASE("relay server rejects empty tx announce payload") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  P2pMessage message;
  message.version = kProtocolVersion;
  message.type = P2pMessageType::kTxAnnounce;
  CHECK(send_message(*socket, message).has_value());

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server answers unknown block height with reject") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
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
  CHECK(server.completed_ok.load());
}

TEST_CASE("relay server rejects oversized frame length prefix") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  CHECK(socket->send_frame_length_prefix(kMaxP2pFrameBytes + 1U).has_value());
  socket->close();

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects truncated frame body") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  constexpr std::uint32_t claimed_len = 512U;
  CHECK(socket->send_frame_length_prefix(claimed_len).has_value());

  const std::vector<std::byte> partial(16, std::byte{0xCD});
  CHECK(socket->send_raw(partial).has_value());
  socket->close();

  server.join();
  CHECK(!server.completed_ok.load());
}

TEST_CASE("relay server rejects disconnect mid-frame") {
  SocketLibrary lib;

  RelayServerThread server(relay_server_config());
  server.wait_for_port();

  auto socket = TcpSocket::connect(TcpEndpoint{.host = "127.0.0.1", .port = server.port.load()});
  CHECK(socket.has_value());
  CHECK(exchange_valid_handshake(*socket));

  P2pMessage ping;
  ping.version = kProtocolVersion;
  ping.type = P2pMessageType::kPing;
  ping.payload = {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},
                  std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}};
  const std::vector<std::byte> wire = ping.to_bytes();
  CHECK(wire.size() > 8);

  CHECK(socket->send_frame_length_prefix(static_cast<std::uint32_t>(wire.size())).has_value());
  const std::span<const std::byte> partial(wire.data(), wire.size() / 2U);
  CHECK(socket->send_raw(partial).has_value());
  socket->close();

  server.join();
  CHECK(!server.completed_ok.load());
}
