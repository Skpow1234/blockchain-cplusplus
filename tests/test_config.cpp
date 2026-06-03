#include <string>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/node/config.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::node::LogLevel;
using blockchain::node::NodeConfig;
using blockchain::node::parse_args;

TEST_CASE("defaults are valid") {
  auto config = parse_args({});
  CHECK(config.has_value());
  CHECK_EQ(config->node_id, std::string("node-0"));
  CHECK(config->log_level == LogLevel::kInfo);
}

TEST_CASE("flags are parsed") {
  auto config = parse_args({"--node-id", "alice", "--log-level", "debug", "--genesis-timestamp",
                            "100", "--data-dir", "/tmp/x"});
  CHECK(config.has_value());
  CHECK_EQ(config->node_id, std::string("alice"));
  CHECK(config->log_level == LogLevel::kDebug);
  CHECK_EQ(config->genesis_timestamp, static_cast<std::uint64_t>(100));
  CHECK_EQ(config->data_dir, std::string("/tmp/x"));
}

TEST_CASE("unknown argument is rejected") {
  auto config = parse_args({"--nope"});
  CHECK(!config.has_value());
  CHECK(config.error().code == ErrorCode::kInvalidConfig);
}

TEST_CASE("missing flag value is rejected") {
  auto config = parse_args({"--node-id"});
  CHECK(!config.has_value());
  CHECK(config.error().code == ErrorCode::kInvalidConfig);
}

TEST_CASE("invalid log level is rejected") {
  auto config = parse_args({"--log-level", "screaming"});
  CHECK(!config.has_value());
  CHECK(config.error().code == ErrorCode::kInvalidConfig);
}

TEST_CASE("non-numeric integer is rejected") {
  auto config = parse_args({"--genesis-timestamp", "soon"});
  CHECK(!config.has_value());
  CHECK(config.error().code == ErrorCode::kInvalidConfig);
}
