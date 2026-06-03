#include "blockchain/node/config.hpp"

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config_json.hpp"

namespace blockchain::node {
namespace {

[[nodiscard]] Result<std::uint64_t> parse_u64(std::string_view text, std::string_view flag) {
  std::uint64_t value = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return make_error(ErrorCode::kInvalidConfig, std::string("invalid unsigned integer for ") +
                                                     std::string(flag) + ": '" + std::string(text) +
                                                     "'");
  }
  return value;
}

[[nodiscard]] Result<std::uint32_t> parse_u32(std::string_view text, std::string_view flag) {
  auto parsed = parse_u64(text, flag);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  if (*parsed > UINT32_MAX) {
    return make_error(ErrorCode::kInvalidConfig,
                      std::string("value out of range for ") + std::string(flag));
  }
  return static_cast<std::uint32_t>(*parsed);
}

}  // namespace

Result<LogLevel> parse_log_level(std::string_view text) {
  if (text == "error") {
    return LogLevel::kError;
  }
  if (text == "warn") {
    return LogLevel::kWarn;
  }
  if (text == "info") {
    return LogLevel::kInfo;
  }
  if (text == "debug") {
    return LogLevel::kDebug;
  }
  if (text == "trace") {
    return LogLevel::kTrace;
  }
  return make_error(ErrorCode::kInvalidConfig,
                    std::string("unknown log level: '") + std::string(text) + "'");
}

std::string_view to_string(LogLevel level) {
  switch (level) {
    case LogLevel::kError:
      return "error";
    case LogLevel::kWarn:
      return "warn";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kDebug:
      return "debug";
    case LogLevel::kTrace:
      return "trace";
  }
  return "info";
}

Result<NetworkMode> parse_network_mode(std::string_view text) {
  if (text == "ping") {
    return NetworkMode::kPing;
  }
  if (text == "relay") {
    return NetworkMode::kRelay;
  }
  return make_error(ErrorCode::kInvalidConfig,
                    std::string("unknown network mode: '") + std::string(text) + "'");
}

std::string_view to_string(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::kPing:
      return "ping";
    case NetworkMode::kRelay:
      return "relay";
  }
  return "ping";
}

Result<void> NodeConfig::validate() const {
  if (node_id.empty()) {
    return make_error(ErrorCode::kInvalidConfig, "node_id must not be empty");
  }
  if (data_dir.empty()) {
    return make_error(ErrorCode::kInvalidConfig, "data_dir must not be empty");
  }
  if (!coinbase_recipient_hex.empty() && !crypto::hash_from_hex(coinbase_recipient_hex)) {
    return make_error(ErrorCode::kInvalidConfig,
                      "coinbase_recipient_hex must be a 64-character hex hash");
  }
  if (listen_port != 0 && peer_port != 0) {
    return make_error(ErrorCode::kInvalidConfig,
                      "specify either --listen-port or --peer, not both");
  }
  if (relay_max_sessions == 0) {
    return make_error(ErrorCode::kInvalidConfig, "relay_max_sessions must be at least 1");
  }
  return {};
}

Result<NodeConfig> parse_args(const std::vector<std::string>& args) {
  NodeConfig config;
  std::optional<std::string> config_file_path;

  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--config") {
      if (i + 1 >= args.size()) {
        return make_error(ErrorCode::kInvalidConfig, "missing value for --config");
      }
      config_file_path = args[++i];
    }
  }

  if (config_file_path) {
    auto loaded = load_config_file(*config_file_path);
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    config = std::move(*loaded);
  }

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (arg == "--help" || arg == "-h") {
      return make_error(ErrorCode::kInvalidConfig, "help requested");
    }
    if (arg == "--config") {
      ++i;
      continue;
    }

    auto require_value = [&](std::string_view flag) -> Result<std::string> {
      if (i + 1 >= args.size()) {
        return make_error(ErrorCode::kInvalidConfig,
                          std::string("missing value for ") + std::string(flag));
      }
      return args[++i];
    };

    if (arg == "--node-id") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      config.node_id = *value;
    } else if (arg == "--data-dir") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      config.data_dir = *value;
    } else if (arg == "--log-level") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto level = parse_log_level(*value);
      if (!level) {
        return std::unexpected(level.error());
      }
      config.log_level = *level;
    } else if (arg == "--genesis-timestamp") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u64(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.genesis_timestamp = *parsed;
    } else if (arg == "--max-block-size") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u64(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.max_block_size_bytes = static_cast<std::uint32_t>(*parsed);
    } else if (arg == "--mine-after-tx") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u32(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.mine_after_tx = *parsed;
    } else if (arg == "--relay-max-sessions") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u32(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.relay_max_sessions = *parsed;
    } else if (arg == "--mine-blocks") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u32(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.mine_blocks = *parsed;
    } else if (arg == "--block-subsidy") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u64(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.block_subsidy = *parsed;
    } else if (arg == "--coinbase-maturity") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u32(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.coinbase_maturity = *parsed;
    } else if (arg == "--coinbase-recipient") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      config.coinbase_recipient_hex = *value;
    } else if (arg == "--listen-host") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      config.listen_host = *value;
    } else if (arg == "--listen-port") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto parsed = parse_u32(*value, arg);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      config.listen_port = static_cast<std::uint16_t>(*parsed);
    } else if (arg == "--persist") {
      config.persist = true;
    } else if (arg == "--restore") {
      config.restore = true;
    } else if (arg == "--network-mode") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto mode = parse_network_mode(*value);
      if (!mode) {
        return std::unexpected(mode.error());
      }
      config.network_mode = *mode;
    } else if (arg == "--peer") {
      auto value = require_value(arg);
      if (!value) {
        return std::unexpected(value.error());
      }
      auto endpoint = net::parse_tcp_endpoint(*value);
      if (!endpoint) {
        return std::unexpected(endpoint.error());
      }
      config.peer_host = endpoint->host;
      config.peer_port = endpoint->port;
    } else {
      return make_error(ErrorCode::kInvalidConfig, std::string("unknown argument: '") + arg + "'");
    }
  }

  if (auto ok = config.validate(); !ok) {
    return std::unexpected(ok.error());
  }
  return config;
}

std::string usage(std::string_view program) {
  std::string out;
  out += "Usage: ";
  out += program;
  out += " [options]\n\n";
  out += "Options:\n";
  out += "  --config <path>            Load JSON config (CLI flags override file)\n";
  out += "  --node-id <id>             Node identifier (default: node-0)\n";
  out += "  --data-dir <path>          Data directory (default: ./data)\n";
  out += "  --persist                  Write ledger.bin after simulator run\n";
  out += "  --restore                  Load ledger.bin instead of mining (simulator)\n";
  out += "  --log-level <level>        error|warn|info|debug|trace (default: info)\n";
  out += "  --genesis-timestamp <n>    Deterministic genesis timestamp (default: 0)\n";
  out += "  --max-block-size <bytes>   Override max block size (default: protocol limit)\n";
  out += "  --mine-blocks <n>          Mine n blocks after genesis (default: 0)\n";
  out += "  --mine-after-tx <n>        Relay: mine n blocks after each tx announce (default: 0)\n";
  out += "  --relay-max-sessions <n>   Relay server: serve n sequential peers (default: 1)\n";
  out += "  --block-subsidy <amount>   Coinbase subsidy per block (default: protocol)\n";
  out += "  --coinbase-maturity <n>    Blocks before a coinbase may be spent (default: protocol)\n";
  out += "  --coinbase-recipient <hex> 64-char hex payout address (default: zero)\n";
  out += "  --network-mode <mode>      ping|relay when using TCP (default: ping)\n";
  out += "  --listen-host <ipv4>        Bind address for TCP server (default: 127.0.0.1)\n";
  out += "  --listen-port <port>        Run TCP server on this port\n";
  out += "  --peer <host:port>         Connect as TCP client to host:port\n";
  out += "  -h, --help                 Show this help and exit\n";
  return out;
}

}  // namespace blockchain::node
