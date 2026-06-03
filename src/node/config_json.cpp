#include "blockchain/node/config_json.hpp"

#include <cctype>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"

namespace blockchain::node {
namespace {

using ConfigValue = std::variant<std::string, std::uint64_t, bool>;

[[nodiscard]] Result<std::uint64_t> parse_u64(std::string_view text, std::string_view label) {
  std::uint64_t value = 0;
  const char* begin = text.data();
  const char* end = begin + text.size();
  const auto [ptr, ec] = std::from_chars(begin, end, value);
  if (ec != std::errc{} || ptr != end) {
    return make_error(ErrorCode::kInvalidConfig,
                      std::string("invalid unsigned integer for ") + std::string(label));
  }
  return value;
}

[[nodiscard]] Result<void> apply_config_field(NodeConfig& config, std::string_view key,
                                              const ConfigValue& value);

struct JsonParser {
  std::string_view input;
  std::size_t pos = 0;

  [[nodiscard]] bool eof() const { return pos >= input.size(); }

  [[nodiscard]] Result<char> peek() const {
    if (eof()) {
      return make_error(ErrorCode::kInvalidConfig, "unexpected end of JSON input");
    }
    return input[pos];
  }

  [[nodiscard]] Result<char> consume() {
    auto ch = peek();
    if (ch) {
      ++pos;
    }
    return ch;
  }

  void skip_ws() {
    while (!eof() && std::isspace(static_cast<unsigned char>(input[pos])) != 0) {
      ++pos;
    }
  }

  [[nodiscard]] Result<void> expect(char expected) {
    auto ch = consume();
    if (!ch) {
      return std::unexpected(ch.error());
    }
    if (*ch != expected) {
      return make_error(ErrorCode::kInvalidConfig,
                        std::string("expected '") + expected + "' in JSON config");
    }
    return {};
  }

  [[nodiscard]] Result<std::string> parse_string() {
    if (auto open = expect('"'); !open) {
      return std::unexpected(open.error());
    }
    std::string out;
    while (true) {
      auto ch = consume();
      if (!ch) {
        return std::unexpected(ch.error());
      }
      if (*ch == '"') {
        return out;
      }
      if (*ch == '\\') {
        return make_error(ErrorCode::kInvalidConfig, "JSON config strings may not use escapes");
      }
      out.push_back(*ch);
    }
  }

  [[nodiscard]] Result<std::uint64_t> parse_uint() {
    skip_ws();
    const std::size_t start = pos;
    if (eof()) {
      return make_error(ErrorCode::kInvalidConfig, "expected JSON number");
    }
    if (input[pos] == '-') {
      return make_error(ErrorCode::kInvalidConfig, "JSON config numbers must be unsigned");
    }
    while (!eof() && std::isdigit(static_cast<unsigned char>(input[pos])) != 0) {
      ++pos;
    }
    if (start == pos) {
      return make_error(ErrorCode::kInvalidConfig, "expected JSON number");
    }
    const std::string_view digits = input.substr(start, pos - start);
    return parse_u64(digits, "config");
  }

  [[nodiscard]] Result<bool> parse_bool() {
    skip_ws();
    if (input.substr(pos, 4) == "true") {
      pos += 4;
      return true;
    }
    if (input.substr(pos, 5) == "false") {
      pos += 5;
      return false;
    }
    return make_error(ErrorCode::kInvalidConfig, "expected JSON boolean");
  }

  [[nodiscard]] Result<ConfigValue> parse_value() {
    skip_ws();
    if (eof()) {
      return make_error(ErrorCode::kInvalidConfig, "expected JSON value");
    }
    const char ch = input[pos];
    if (ch == '"') {
      auto str = parse_string();
      if (!str) {
        return std::unexpected(str.error());
      }
      return ConfigValue{std::move(*str)};
    }
    if (ch == 't' || ch == 'f') {
      auto value = parse_bool();
      if (!value) {
        return std::unexpected(value.error());
      }
      return ConfigValue{*value};
    }
    if (ch == 'n') {
      if (input.substr(pos, 4) == "null") {
        pos += 4;
        return ConfigValue{std::string{}};
      }
      return make_error(ErrorCode::kInvalidConfig, "null is not a supported config value");
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      auto value = parse_uint();
      if (!value) {
        return std::unexpected(value.error());
      }
      return ConfigValue{*value};
    }
    return make_error(ErrorCode::kInvalidConfig, "unsupported JSON value in config");
  }

  [[nodiscard]] Result<void> parse_object(NodeConfig& config) {
    if (auto open = expect('{'); !open) {
      return std::unexpected(open.error());
    }
    skip_ws();
    if (auto ch = peek(); ch && *ch == '}') {
      if (auto close = expect('}'); !close) {
        return std::unexpected(close.error());
      }
      return {};
    }

    while (true) {
      auto key = parse_string();
      if (!key) {
        return std::unexpected(key.error());
      }
      if (auto colon = expect(':'); !colon) {
        return std::unexpected(colon.error());
      }
      auto value = parse_value();
      if (!value) {
        return std::unexpected(value.error());
      }
      if (auto applied = apply_config_field(config, *key, *value); !applied) {
        return std::unexpected(applied.error());
      }

      skip_ws();
      auto sep = consume();
      if (!sep) {
        return std::unexpected(sep.error());
      }
      if (*sep == '}') {
        return {};
      }
      if (*sep != ',') {
        return make_error(ErrorCode::kInvalidConfig, "expected ',' or '}' in JSON config");
      }
      skip_ws();
    }
  }
};

[[nodiscard]] bool is_ignored_key(std::string_view key) {
  return !key.empty() && key[0] == '_';
}

[[nodiscard]] Result<void> apply_string_field(NodeConfig& config, std::string_view key,
                                              const std::string& value) {
  if (key == "node_id") {
    config.node_id = value;
    return {};
  }
  if (key == "data_dir") {
    config.data_dir = value;
    return {};
  }
  if (key == "log_level") {
    auto level = parse_log_level(value);
    if (!level) {
      return std::unexpected(level.error());
    }
    config.log_level = *level;
    return {};
  }
  if (key == "coinbase_recipient_hex") {
    config.coinbase_recipient_hex = value;
    return {};
  }
  if (key == "listen_host") {
    config.listen_host = value;
    return {};
  }
  if (key == "port_file") {
    config.port_file = value;
    return {};
  }
  if (key == "network_mode") {
    auto mode = parse_network_mode(value);
    if (!mode) {
      return std::unexpected(mode.error());
    }
    config.network_mode = *mode;
    return {};
  }
  if (key == "peer") {
    if (value.empty()) {
      config.peer_host.clear();
      config.peer_port = 0;
      return {};
    }
    auto endpoint = net::parse_tcp_endpoint(value);
    if (!endpoint) {
      return std::unexpected(endpoint.error());
    }
    config.peer_host = endpoint->host;
    config.peer_port = endpoint->port;
    return {};
  }
  return make_error(ErrorCode::kInvalidConfig,
                    std::string("unknown config key: '") + std::string(key) + "'");
}

[[nodiscard]] Result<void> apply_uint_field(NodeConfig& config, std::string_view key,
                                            std::uint64_t value) {
  if (key == "genesis_timestamp") {
    config.genesis_timestamp = value;
    return {};
  }
  if (key == "block_subsidy") {
    config.block_subsidy = value;
    return {};
  }
  if (key == "max_block_size_bytes") {
    config.max_block_size_bytes = static_cast<std::uint32_t>(value);
    return {};
  }
  if (key == "mine_blocks") {
    config.mine_blocks = static_cast<std::uint32_t>(value);
    return {};
  }
  if (key == "mine_after_tx") {
    config.mine_after_tx = static_cast<std::uint32_t>(value);
    return {};
  }
  if (key == "relay_max_sessions") {
    config.relay_max_sessions = static_cast<std::uint32_t>(value);
    return {};
  }
  if (key == "coinbase_maturity") {
    config.coinbase_maturity = static_cast<std::uint32_t>(value);
    return {};
  }
  if (key == "listen_port") {
    if (value > UINT16_MAX) {
      return make_error(ErrorCode::kInvalidConfig, "listen_port out of range");
    }
    config.listen_port = static_cast<std::uint16_t>(value);
    config.listen_enabled = true;
    return {};
  }
  return make_error(ErrorCode::kInvalidConfig,
                    std::string("unknown config key: '") + std::string(key) + "'");
}

[[nodiscard]] Result<void> apply_bool_field(NodeConfig& config, std::string_view key, bool value) {
  if (key == "persist") {
    config.persist = value;
    return {};
  }
  if (key == "restore") {
    config.restore = value;
    return {};
  }
  return make_error(ErrorCode::kInvalidConfig,
                    std::string("unknown config key: '") + std::string(key) + "'");
}

[[nodiscard]] Result<void> apply_config_field(NodeConfig& config, std::string_view key,
                                              const ConfigValue& value) {
  if (is_ignored_key(key)) {
    return {};
  }
  if (std::holds_alternative<std::string>(value)) {
    return apply_string_field(config, key, std::get<std::string>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return apply_bool_field(config, key, std::get<bool>(value));
  }
  return apply_uint_field(config, key, std::get<std::uint64_t>(value));
}

}  // namespace

Result<NodeConfig> parse_config_json(std::string_view json) {
  NodeConfig config;
  JsonParser parser{.input = json};
  if (auto ok = parser.parse_object(config); !ok) {
    return std::unexpected(ok.error());
  }
  parser.skip_ws();
  if (!parser.eof()) {
    return make_error(ErrorCode::kInvalidConfig, "trailing data after JSON config object");
  }
  if (auto validated = config.validate(); !validated) {
    return std::unexpected(validated.error());
  }
  return config;
}

Result<NodeConfig> load_config_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return make_error(ErrorCode::kInvalidConfig, "cannot open config file: " + path);
  }
  const auto size = input.tellg();
  if (size < 0) {
    return make_error(ErrorCode::kInvalidConfig, "config file size is invalid");
  }
  if (static_cast<std::size_t>(size) > kMaxConfigFileBytes) {
    return make_error(ErrorCode::kResourceLimitExceeded, "config file is too large");
  }
  std::string bytes(static_cast<std::size_t>(size), '\0');
  input.seekg(0, std::ios::beg);
  input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    return make_error(ErrorCode::kInvalidConfig, "config file read failed");
  }
  return parse_config_json(bytes);
}

}  // namespace blockchain::node
