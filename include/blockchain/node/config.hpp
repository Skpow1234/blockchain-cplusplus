#ifndef BLOCKCHAIN_NODE_CONFIG_HPP
#define BLOCKCHAIN_NODE_CONFIG_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "blockchain/error.hpp"

namespace blockchain::node {

// Logging verbosity. Ordered from least to most verbose.
enum class LogLevel : std::uint8_t { kError = 0, kWarn, kInfo, kDebug, kTrace };

// Stage-2 TCP behavior when --listen-port or --peer is set.
enum class NetworkMode : std::uint8_t {
  kPing = 0,  // handshake + single ping/pong (default)
  kRelay,     // handshake + block/tx relay (request/response sync)
};

[[nodiscard]] Result<LogLevel> parse_log_level(std::string_view text);
[[nodiscard]] std::string_view to_string(LogLevel level);

[[nodiscard]] Result<NetworkMode> parse_network_mode(std::string_view text);
[[nodiscard]] std::string_view to_string(NetworkMode mode);

// Runtime configuration for a node/simulator instance. Nothing here is
// hardcoded into the binary: fields come from CLI flags and/or a JSON config
// file (--config) and are validated before use.
struct NodeConfig {
  // Scalars grouped first to avoid excess padding between std::string members.
  std::uint64_t genesis_timestamp = 0;
  std::uint64_t block_subsidy = 0;
  std::uint32_t max_block_size_bytes = 0;  // 0 => use protocol default
  std::uint32_t mine_blocks = 0;
  std::uint32_t mine_after_tx = 0;
  // Relay server: accept and serve this many sequential peer connections (>= 1).
  std::uint32_t relay_max_sessions = 1;
  std::uint32_t coinbase_maturity = 0;
  std::uint16_t listen_port = 0;
  std::uint16_t peer_port = 0;
  LogLevel log_level = LogLevel::kInfo;
  bool persist = false;
  bool restore = false;
  NetworkMode network_mode = NetworkMode::kPing;

  std::string node_id = "node-0";
  std::string data_dir = "./data";
  std::string coinbase_recipient_hex;
  std::string listen_host = "127.0.0.1";
  std::string peer_host;

  // Validates the configuration, returning a descriptive error on failure.
  [[nodiscard]] Result<void> validate() const;
};

// Parses argv into a NodeConfig. Returns kInvalidConfig with a helpful message
// on malformed input. A request for --help is reported as a (handled) error so
// the caller can print usage and exit cleanly.
[[nodiscard]] Result<NodeConfig> parse_args(const std::vector<std::string>& args);

[[nodiscard]] std::string usage(std::string_view program);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_CONFIG_HPP
