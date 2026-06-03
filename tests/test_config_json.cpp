#include <fstream>
#include <string>

#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/config_json.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::node::load_config_file;
using blockchain::node::LogLevel;
using blockchain::node::NetworkMode;
using blockchain::node::parse_args;
using blockchain::node::parse_config_json;

namespace {

void write_text(const std::string& path, std::string_view content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

}  // namespace

TEST_CASE("parse_config_json reads known fields") {
  const std::string json = R"({
    "_comment": "ignored",
    "node_id": "cfg-node",
    "data_dir": "/tmp/cfg",
    "log_level": "debug",
    "genesis_timestamp": 42,
    "mine_blocks": 3,
    "block_subsidy": 900,
    "network_mode": "relay",
    "persist": true
  })";

  auto config = parse_config_json(json);
  CHECK(config.has_value());
  CHECK_EQ(config->node_id, std::string("cfg-node"));
  CHECK_EQ(config->data_dir, std::string("/tmp/cfg"));
  CHECK(config->log_level == LogLevel::kDebug);
  CHECK_EQ(config->genesis_timestamp, static_cast<std::uint64_t>(42));
  CHECK_EQ(config->mine_blocks, static_cast<std::uint32_t>(3));
  CHECK_EQ(config->block_subsidy, static_cast<std::uint64_t>(900));
  CHECK(config->network_mode == NetworkMode::kRelay);
  CHECK(config->persist);
}

TEST_CASE("parse_config_json rejects unknown keys") {
  const std::string json = R"({ "not_a_real_field": 1 })";
  auto config = parse_config_json(json);
  CHECK(!config.has_value());
  CHECK(config.error().code == ErrorCode::kInvalidConfig);
}

TEST_CASE("parse_config_json rejects trailing garbage") {
  const std::string json = R"({ "node_id": "x" }) trailing";
  auto config = parse_config_json(json);
  CHECK(!config.has_value());
}

TEST_CASE("CLI overrides JSON config file values") {
  const std::string path = "test_config_json_override.json";
  write_text(path, R"json({ "node_id": "from-file", "mine_blocks": 1 })json");

  auto config = parse_args({"--config", path, "--node-id", "from-cli", "--mine-blocks", "9"});
  CHECK(config.has_value());
  CHECK_EQ(config->node_id, std::string("from-cli"));
  CHECK_EQ(config->mine_blocks, static_cast<std::uint32_t>(9));

  (void)std::remove(path.c_str());
}

TEST_CASE("load_config_file reads from disk") {
  const std::string path = "test_config_json_on_disk.json";
  write_text(path, R"json({
    "node_id": "disk-node",
    "data_dir": "./data/disk",
    "log_level": "warn"
  })json");

  auto config = load_config_file(path);
  CHECK(config.has_value());
  CHECK_EQ(config->node_id, std::string("disk-node"));
  CHECK_EQ(config->data_dir, std::string("./data/disk"));
  CHECK(config->log_level == LogLevel::kWarn);

  (void)std::remove(path.c_str());
}
