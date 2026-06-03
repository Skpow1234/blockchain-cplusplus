#ifndef BLOCKCHAIN_NODE_NETWORK_HPP
#define BLOCKCHAIN_NODE_NETWORK_HPP

#include "blockchain/error.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"

namespace blockchain::node {

// Stage-2 local network: one-shot handshake + ping/pong over TCP using framed
// P2P messages. Listens on --listen-host/--listen-port or connects via --peer.
[[nodiscard]] bool network_mode_enabled(const NodeConfig& config) noexcept;

// Serves one already-accepted TCP connection (handshake + ping/pong).
[[nodiscard]] Result<void> serve_ping_connection(net::TcpSocket& socket, const NodeConfig& config);

// Server: bind, accept one connection, perform handshake, respond to ping with pong.
[[nodiscard]] Result<void> run_ping_server(const NodeConfig& config);

// Client: connect, perform handshake, send ping and verify pong echo.
[[nodiscard]] Result<void> run_ping_client(const NodeConfig& config);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_NETWORK_HPP
