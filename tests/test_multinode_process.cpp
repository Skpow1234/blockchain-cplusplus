// Multi-process integration: spawn real blockchain_node server and client processes.
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "blockchain/crypto/hash.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/storage/chain_store.hpp"
#include "process_spawn.hpp"
#include "testing.hpp"

using bctest::ChildProcess;
using bctest::read_port_file;
using bctest::spawn_process;
using bctest::wait_for_port_file;
using bctest::wait_process;
using blockchain::crypto::Hash256;
using blockchain::crypto::zero_hash;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::storage::ChainStore;

#ifndef BLOCKCHAIN_NODE_PATH
#define BLOCKCHAIN_NODE_PATH "blockchain_node"
#endif

namespace {

std::string node_binary() {
  return BLOCKCHAIN_NODE_PATH;
}

bool binary_exists(const std::string& path) {
#ifdef _WIN32
  return _access(path.c_str(), 0) == 0;
#else
  return access(path.c_str(), F_OK) == 0;
#endif
}

std::string temp_dir(const char* prefix) {
  static int counter = 0;
  const std::string dir = std::string(prefix) + std::to_string(++counter);
  (void)std::remove((dir + "/ledger.bin").c_str());
  (void)std::remove((dir + "/ledger.bin.tmp").c_str());
  (void)std::remove((dir + "/port.txt").c_str());
#ifdef _WIN32
  (void)_mkdir(dir.c_str());
#else
  (void)mkdir(dir.c_str(), 0755);
#endif
  return dir;
}

std::vector<std::string> base_node_args() {
  return {node_binary(),
          "--genesis-timestamp",
          "1700000100",
          "--block-subsidy",
          "5000",
          "--coinbase-maturity",
          "1",
          "--coinbase-recipient",
          "a100000000000000000000000000000000000000000000000000000000000000"};
}

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

Transaction make_spend(const OutPoint& prevout, std::uint64_t input_value, std::uint64_t fee,
                       const Hash256& to) {
  Transaction tx;
  TxInput input;
  input.prevout = prevout;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = input_value - fee;
  output.recipient = to;
  tx.outputs.push_back(output);
  return tx;
}

NodeConfig relay_params() {
  NodeConfig config;
  config.genesis_timestamp = 1'700'000'100;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex =
      "a100000000000000000000000000000000000000000000000000000000000000";
  return config;
}

Transaction coinbase_spend_after_block1() {
  NodeConfig miner = relay_params();
  miner.mine_blocks = 1;
  auto state = PeerState::from_config(miner);
  CHECK(state.has_value());
  const auto* block1 = state->block_at_height(1);
  CHECK(block1 != nullptr);
  OutPoint coinbase_out;
  coinbase_out.txid = block1->transactions.front().txid();
  coinbase_out.index = 0;
  return make_spend(coinbase_out, 5'000, 100, tag_hash(0xB2));
}

bool write_bytes_file(const std::string& path, std::span<const std::byte> bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

std::vector<std::string> relay_client_args(const std::string& data_dir, std::uint16_t port) {
  std::vector<std::string> args = base_node_args();
  args.push_back("--data-dir");
  args.push_back(data_dir);
  args.push_back("--network-mode");
  args.push_back("relay");
  args.push_back("--persist");
  args.push_back("--peer");
  args.push_back("127.0.0.1:" + std::to_string(port));
  return args;
}

}  // namespace

TEST_CASE("multiprocess ping server and client processes") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const std::string work = temp_dir("test_multinode_ping_");
  const std::string port_file = work + "/port.txt";

  std::vector<std::string> server_args = base_node_args();
  server_args.push_back("--network-mode");
  server_args.push_back("ping");
  server_args.push_back("--listen-port");
  server_args.push_back("0");
  server_args.push_back("--port-file");
  server_args.push_back(port_file);

  ChildProcess server = spawn_process(server_args);
  CHECK(server.valid());
  CHECK(wait_for_port_file(port_file));

  const std::uint16_t port = read_port_file(port_file);
  CHECK(port != 0);

  std::vector<std::string> client_args = base_node_args();
  client_args.push_back("--network-mode");
  client_args.push_back("ping");
  client_args.push_back("--peer");
  client_args.push_back("127.0.0.1:" + std::to_string(port));

  ChildProcess client = spawn_process(client_args);
  CHECK(client.valid());
  CHECK_EQ(wait_process(client), 0);
  CHECK_EQ(wait_process(server), 0);
}

TEST_CASE("multiprocess relay server publishes blocks to client process") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const std::string server_dir = temp_dir("test_multinode_srv_");
  const std::string client_dir = temp_dir("test_multinode_cli_");
  const std::string port_file = server_dir + "/port.txt";

  std::vector<std::string> server_args = base_node_args();
  server_args.push_back("--data-dir");
  server_args.push_back(server_dir);
  server_args.push_back("--network-mode");
  server_args.push_back("relay");
  server_args.push_back("--mine-blocks");
  server_args.push_back("2");
  server_args.push_back("--listen-port");
  server_args.push_back("0");
  server_args.push_back("--port-file");
  server_args.push_back(port_file);

  ChildProcess server = spawn_process(server_args);
  CHECK(server.valid());
  CHECK(wait_for_port_file(port_file));

  const std::uint16_t port = read_port_file(port_file);
  CHECK(port != 0);

  std::vector<std::string> client_args = base_node_args();
  client_args.push_back("--data-dir");
  client_args.push_back(client_dir);
  client_args.push_back("--network-mode");
  client_args.push_back("relay");
  client_args.push_back("--persist");
  client_args.push_back("--peer");
  client_args.push_back("127.0.0.1:" + std::to_string(port));

  ChildProcess client = spawn_process(client_args);
  CHECK(client.valid());
  CHECK_EQ(wait_process(client), 0);
  CHECK_EQ(wait_process(server), 0);

  CHECK(ChainStore(client_dir).ledger_exists());
  auto chain = ChainStore(client_dir).load_chain();
  CHECK(chain.has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(2));
}

TEST_CASE("multiprocess relay sync is deterministic across two client runs") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const auto run_once = []() -> Hash256 {
    const std::string server_dir = temp_dir("test_multinode_det_srv_");
    const std::string client_dir = temp_dir("test_multinode_det_cli_");
    const std::string port_file = server_dir + "/port.txt";

    std::vector<std::string> server_args = base_node_args();
    server_args.push_back("--data-dir");
    server_args.push_back(server_dir);
    server_args.push_back("--network-mode");
    server_args.push_back("relay");
    server_args.push_back("--mine-blocks");
    server_args.push_back("1");
    server_args.push_back("--listen-port");
    server_args.push_back("0");
    server_args.push_back("--port-file");
    server_args.push_back(port_file);

    ChildProcess server = spawn_process(server_args);
    if (!server.valid() || !wait_for_port_file(port_file)) {
      return {};
    }
    const std::uint16_t port = read_port_file(port_file);

    std::vector<std::string> client_args = base_node_args();
    client_args.push_back("--data-dir");
    client_args.push_back(client_dir);
    client_args.push_back("--network-mode");
    client_args.push_back("relay");
    client_args.push_back("--persist");
    client_args.push_back("--peer");
    client_args.push_back("127.0.0.1:" + std::to_string(port));

    ChildProcess client = spawn_process(client_args);
    if (!client.valid() || wait_process(client) != 0 || wait_process(server) != 0) {
      return {};
    }

    auto chain = ChainStore(client_dir).load_chain();
    if (!chain) {
      return {};
    }
    return chain->tip_hash();
  };

  const Hash256 first = run_once();
  const Hash256 second = run_once();
  CHECK(first != zero_hash());
  CHECK(first == second);
}

TEST_CASE("multiprocess relay seed serves two sequential client processes") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const std::string server_dir = temp_dir("test_multinode_2cli_srv_");
  const std::string client1_dir = temp_dir("test_multinode_2cli_c1_");
  const std::string client2_dir = temp_dir("test_multinode_2cli_c2_");
  const std::string port_file = server_dir + "/port.txt";

  std::vector<std::string> server_args = base_node_args();
  server_args.push_back("--data-dir");
  server_args.push_back(server_dir);
  server_args.push_back("--network-mode");
  server_args.push_back("relay");
  server_args.push_back("--mine-blocks");
  server_args.push_back("2");
  server_args.push_back("--relay-max-sessions");
  server_args.push_back("2");
  server_args.push_back("--listen-port");
  server_args.push_back("0");
  server_args.push_back("--port-file");
  server_args.push_back(port_file);

  ChildProcess server = spawn_process(server_args);
  CHECK(server.valid());
  CHECK(wait_for_port_file(port_file));
  const std::uint16_t port = read_port_file(port_file);
  CHECK(port != 0);

  ChildProcess client1 = spawn_process(relay_client_args(client1_dir, port));
  CHECK(client1.valid());
  CHECK_EQ(wait_process(client1), 0);

  ChildProcess client2 = spawn_process(relay_client_args(client2_dir, port));
  CHECK(client2.valid());
  CHECK_EQ(wait_process(client2), 0);
  CHECK_EQ(wait_process(server), 0);

  auto chain1 = ChainStore(client1_dir).load_chain();
  auto chain2 = ChainStore(client2_dir).load_chain();
  CHECK(chain1.has_value());
  CHECK(chain2.has_value());
  CHECK_EQ(chain1->height(), static_cast<std::uint32_t>(2));
  CHECK_EQ(chain2->height(), static_cast<std::uint32_t>(2));
  CHECK(chain1->tip_hash() == chain2->tip_hash());
}

TEST_CASE("multiprocess relay client reconnects after disconnect") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const std::string server_dir = temp_dir("test_multinode_recon_srv_");
  const std::string client_dir = temp_dir("test_multinode_recon_cli_");
  const std::string port_file = server_dir + "/port.txt";

  std::vector<std::string> server_args = base_node_args();
  server_args.push_back("--data-dir");
  server_args.push_back(server_dir);
  server_args.push_back("--network-mode");
  server_args.push_back("relay");
  server_args.push_back("--mine-blocks");
  server_args.push_back("1");
  server_args.push_back("--relay-max-sessions");
  server_args.push_back("2");
  server_args.push_back("--listen-port");
  server_args.push_back("0");
  server_args.push_back("--port-file");
  server_args.push_back(port_file);

  ChildProcess server = spawn_process(server_args);
  CHECK(server.valid());
  CHECK(wait_for_port_file(port_file));
  const std::uint16_t port = read_port_file(port_file);

  ChildProcess first = spawn_process(relay_client_args(client_dir, port));
  CHECK(first.valid());
  CHECK_EQ(wait_process(first), 0);

  std::vector<std::string> second_args = relay_client_args(client_dir, port);
  second_args.push_back("--restore");
  ChildProcess second = spawn_process(second_args);
  CHECK(second.valid());
  CHECK_EQ(wait_process(second), 0);
  CHECK_EQ(wait_process(server), 0);

  auto chain = ChainStore(client_dir).load_chain();
  CHECK(chain.has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(1));
}

TEST_CASE("multiprocess relay propagates announced transaction into a new block") {
  if (!binary_exists(node_binary())) {
    std::cout << "[SKIP] blockchain_node binary not found at " << node_binary() << "\n";
    return;
  }

  const std::string server_dir = temp_dir("test_multinode_tx_srv_");
  const std::string client_dir = temp_dir("test_multinode_tx_cli_");
  const std::string port_file = server_dir + "/port.txt";
  const std::string tx_file = client_dir + "/spend.tx";

#ifdef _WIN32
  (void)_mkdir(client_dir.c_str());
#else
  (void)mkdir(client_dir.c_str(), 0755);
#endif

  const Transaction spend = coinbase_spend_after_block1();
  const auto tx_bytes = spend.to_bytes();
  CHECK(write_bytes_file(tx_file, tx_bytes));

  std::vector<std::string> server_args = base_node_args();
  server_args.push_back("--data-dir");
  server_args.push_back(server_dir);
  server_args.push_back("--network-mode");
  server_args.push_back("relay");
  server_args.push_back("--mine-blocks");
  server_args.push_back("1");
  server_args.push_back("--mine-after-tx");
  server_args.push_back("1");
  server_args.push_back("--listen-port");
  server_args.push_back("0");
  server_args.push_back("--port-file");
  server_args.push_back(port_file);

  ChildProcess server = spawn_process(server_args);
  CHECK(server.valid());
  CHECK(wait_for_port_file(port_file));
  const std::uint16_t port = read_port_file(port_file);

  std::vector<std::string> client_args = relay_client_args(client_dir, port);
  client_args.push_back("--announce-tx-file");
  client_args.push_back(tx_file);
  client_args.push_back("--relay-blocks-after-tx");
  client_args.push_back("1");

  ChildProcess client = spawn_process(client_args);
  CHECK(client.valid());
  CHECK_EQ(wait_process(client), 0);
  CHECK_EQ(wait_process(server), 0);

  auto chain = ChainStore(client_dir).load_chain();
  CHECK(chain.has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(2));
}
