#include "blockchain/node/network.hpp"

#include "blockchain/crypto/hash.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/net/socket_io.hpp"

namespace blockchain::node {
namespace {

[[nodiscard]] net::HandshakePayload local_handshake(const NodeConfig& config) {
  net::HandshakePayload hs;
  hs.network_magic = net::kNetworkMagic;
  hs.height = 0;
  hs.tip_hash = crypto::zero_hash();
  hs.node_id = config.node_id;
  return hs;
}

[[nodiscard]] Result<net::HandshakePayload> expect_handshake(const net::P2pMessage& message) {
  return net::parse_handshake_message(message);
}

[[nodiscard]] Result<void> exchange_handshake(net::TcpSocket& socket, const NodeConfig& config) {
  auto outbound = net::make_handshake_message(local_handshake(config));
  if (!outbound) {
    return std::unexpected(outbound.error());
  }
  if (auto sent = net::send_message(socket, *outbound); !sent) {
    return sent;
  }
  auto inbound = net::recv_message(socket);
  if (!inbound) {
    return std::unexpected(inbound.error());
  }
  if (auto peer = expect_handshake(*inbound); !peer) {
    return std::unexpected(peer.error());
  }
  return {};
}

[[nodiscard]] Result<void> serve_handshake(net::TcpSocket& socket, const NodeConfig& config) {
  auto inbound = net::recv_message(socket);
  if (!inbound) {
    return std::unexpected(inbound.error());
  }
  if (auto peer = expect_handshake(*inbound); !peer) {
    return std::unexpected(peer.error());
  }
  auto outbound = net::make_handshake_message(local_handshake(config));
  if (!outbound) {
    return std::unexpected(outbound.error());
  }
  return net::send_message(socket, *outbound);
}

[[nodiscard]] std::uint64_t ping_nonce(const NodeConfig& config) noexcept {
  // Deterministic nonce derived from config (not wall clock).
  return config.genesis_timestamp != 0 ? config.genesis_timestamp : 1;
}

}  // namespace

bool network_mode_enabled(const NodeConfig& config) noexcept {
  return config.listen_port != 0 || config.peer_port != 0;
}

Result<void> serve_ping_connection(net::TcpSocket& socket, const NodeConfig& config) {
  if (auto hs = serve_handshake(socket, config); !hs) {
    return hs;
  }

  auto inbound = net::recv_message(socket);
  if (!inbound) {
    return std::unexpected(inbound.error());
  }
  auto ping = net::parse_ping_message(*inbound);
  if (!ping) {
    return std::unexpected(ping.error());
  }

  auto pong = net::make_pong_message(net::PongPayload{.nonce = ping->nonce});
  if (!pong) {
    return std::unexpected(pong.error());
  }
  return net::send_message(socket, *pong);
}

Result<void> run_ping_server(const NodeConfig& config) {
  net::SocketLibrary lib;
  net::TcpEndpoint endpoint{.host = config.listen_host, .port = config.listen_port};

  auto listener = net::TcpListener::bind(endpoint);
  if (!listener) {
    return std::unexpected(listener.error());
  }

  auto client = listener->accept();
  if (!client) {
    return std::unexpected(client.error());
  }

  return serve_ping_connection(*client, config);
}

Result<void> run_ping_client(const NodeConfig& config) {
  net::SocketLibrary lib;
  net::TcpEndpoint endpoint{.host = config.peer_host, .port = config.peer_port};

  auto socket = net::TcpSocket::connect(endpoint);
  if (!socket) {
    return std::unexpected(socket.error());
  }

  if (auto hs = exchange_handshake(*socket, config); !hs) {
    return hs;
  }

  auto ping = net::make_ping_message(net::PingPayload{.nonce = ping_nonce(config)});
  if (!ping) {
    return std::unexpected(ping.error());
  }
  if (auto sent = net::send_message(*socket, *ping); !sent) {
    return sent;
  }

  auto inbound = net::recv_message(*socket);
  if (!inbound) {
    return std::unexpected(inbound.error());
  }
  auto pong = net::parse_pong_message(*inbound);
  if (!pong) {
    return std::unexpected(pong.error());
  }
  if (pong->nonce != ping_nonce(config)) {
    return make_error(ErrorCode::kInvalidMessage, "pong nonce does not match ping");
  }
  return {};
}

}  // namespace blockchain::node
