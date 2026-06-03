#ifndef BLOCKCHAIN_NODE_NETWORK_HPP
#define BLOCKCHAIN_NODE_NETWORK_HPP

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/protocol/transaction.hpp"

namespace blockchain::node {

// Stage-2 local network: one-shot handshake + ping/pong over TCP using framed
// P2P messages. Listens on --listen-host/--listen-port or connects via --peer.
[[nodiscard]] bool is_network_server(const NodeConfig& config) noexcept;
[[nodiscard]] bool is_network_client(const NodeConfig& config) noexcept;
[[nodiscard]] bool network_mode_enabled(const NodeConfig& config) noexcept;

// Serves one already-accepted TCP connection (handshake + ping/pong).
[[nodiscard]] Result<void> serve_ping_connection(net::TcpSocket& socket, const NodeConfig& config);

// Server: bind, accept one connection, perform handshake, respond to ping with pong.
[[nodiscard]] Result<void> run_ping_server(const NodeConfig& config);

// Client: connect, perform handshake, send ping and verify pong echo.
[[nodiscard]] Result<void> run_ping_client(const NodeConfig& config);

// Relay mode: chain-aware handshake plus block request/response sync and tx/block
// relay handling. Uses --mine-blocks on the server to publish blocks to peers.

struct RelaySessionSummary {
  std::uint32_t height = 0;
  std::size_t mempool_size = 0;
};

struct RelayClientResult {
  std::uint32_t height = 0;
  crypto::Hash256 tip_hash{};
};

struct RelayClientOptions {
  // Transactions to announce on the wire after block sync completes.
  std::vector<protocol::Transaction> txs_after_sync;
  // After tx announces, request this many additional blocks from the peer.
  std::uint32_t blocks_after_tx = 0;
};

struct RelayServerOptions {
  // Number of sequential accept/serve cycles before the listener closes.
  // Zero means use NodeConfig::relay_max_sessions.
  std::uint32_t max_sessions = 0;
  // When non-null, set to the bound port after listen (for ephemeral port tests).
  std::atomic<std::uint16_t>* port_ready = nullptr;
};

struct RelayServerResult {
  std::uint32_t sessions_completed = 0;
  std::uint16_t listen_port = 0;
  RelaySessionSummary last_session;
};

// Serves one peer using existing chain/mempool state (shared across server sessions).
[[nodiscard]] Result<RelaySessionSummary> serve_relay_connection(net::TcpSocket& socket,
                                                                 PeerState& state);

// Bootstraps state from config, serves one peer, then discards state.
[[nodiscard]] Result<RelaySessionSummary> serve_relay_connection(net::TcpSocket& socket,
                                                                 const NodeConfig& config);

[[nodiscard]] Result<RelayServerResult> run_relay_server(const NodeConfig& config,
                                                         const RelayServerOptions& options = {});

[[nodiscard]] Result<RelayClientResult> run_relay_client(const NodeConfig& config,
                                                         const RelayClientOptions& options = {});

// Loads a canonical transaction from a file (bounded by kMaxTransactionSizeBytes).
[[nodiscard]] Result<protocol::Transaction> load_transaction_file(const std::string& path);

// Builds relay client options from config (loads --announce-tx-file paths).
[[nodiscard]] Result<RelayClientOptions> relay_client_options_from_config(const NodeConfig& config);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_NETWORK_HPP
