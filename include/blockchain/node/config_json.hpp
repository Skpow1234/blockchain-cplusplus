#ifndef BLOCKCHAIN_NODE_CONFIG_JSON_HPP
#define BLOCKCHAIN_NODE_CONFIG_JSON_HPP

#include <string>
#include <string_view>

#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"

namespace blockchain::node {

// Maximum config file size before read (hostile input guard).
inline constexpr std::size_t kMaxConfigFileBytes = 64U * 1024U;

// Parses a JSON object with known NodeConfig fields. Rejects unknown root types,
// trailing garbage, and keys that do not map to configuration (except "_*").
[[nodiscard]] Result<NodeConfig> parse_config_json(std::string_view json);

// Reads a config file from disk and parses it with the same rules.
[[nodiscard]] Result<NodeConfig> load_config_file(const std::string& path);

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_CONFIG_JSON_HPP
