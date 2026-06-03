#include "blockchain/node/config.hpp"

#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace blockchain::node {
namespace {

[[nodiscard]] Result<std::uint64_t> parse_u64(std::string_view text, std::string_view flag) {
  std::uint64_t value = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return make_error(ErrorCode::kInvalidConfig,
                      std::string("invalid unsigned integer for ") + std::string(flag) + ": '" +
                          std::string(text) + "'");
  }
  return value;
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

Result<void> NodeConfig::validate() const {
  if (node_id.empty()) {
    return make_error(ErrorCode::kInvalidConfig, "node_id must not be empty");
  }
  if (data_dir.empty()) {
    return make_error(ErrorCode::kInvalidConfig, "data_dir must not be empty");
  }
  return {};
}

Result<NodeConfig> parse_args(const std::vector<std::string>& args) {
  NodeConfig config;

  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (arg == "--help" || arg == "-h") {
      return make_error(ErrorCode::kInvalidConfig, "help requested");
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
  out += "  --node-id <id>             Node identifier (default: node-0)\n";
  out += "  --data-dir <path>          Data directory (default: ./data)\n";
  out += "  --log-level <level>        error|warn|info|debug|trace (default: info)\n";
  out += "  --genesis-timestamp <n>    Deterministic genesis timestamp (default: 0)\n";
  out += "  --max-block-size <bytes>   Override max block size (default: protocol limit)\n";
  out += "  -h, --help                 Show this help and exit\n";
  return out;
}

}  // namespace blockchain::node
