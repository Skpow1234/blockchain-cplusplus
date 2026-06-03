#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/network.hpp"
#include "blockchain/node/simulator.hpp"

namespace {

int run(const std::vector<std::string>& args, std::string_view program) {
  auto config = blockchain::node::parse_args(args);
  if (!config) {
    if (config.error().message == "help requested") {
      std::cout << blockchain::node::usage(program);
      return 0;
    }
    std::cerr << "configuration error: " << config.error().message << "\n\n"
              << blockchain::node::usage(program);
    return 2;
  }

  if (blockchain::node::network_mode_enabled(*config)) {
    if (config->listen_port != 0) {
      auto ok = blockchain::node::run_ping_server(*config);
      if (!ok) {
        std::cerr << "network error: " << ok.error().message << "\n";
        return 1;
      }
      std::cout << "ping server completed on " << config->listen_host << ":"
                << config->listen_port << "\n";
      return 0;
    }
    auto ok = blockchain::node::run_ping_client(*config);
    if (!ok) {
      std::cerr << "network error: " << ok.error().message << "\n";
      return 1;
    }
    std::cout << "ping client completed (" << config->peer_host << ":" << config->peer_port
              << ")\n";
    return 0;
  }

  auto summary = blockchain::node::run_simulator(*config);
  if (!summary) {
    std::cerr << "simulator error: " << summary.error().message << "\n";
    return 1;
  }

  std::cout << "blockchain simulator (stage 1)\n"
            << "  node id:        " << config->node_id << "\n"
            << "  data dir:       " << config->data_dir << "\n"
            << "  log level:      " << blockchain::node::to_string(config->log_level) << "\n"
            << "  genesis time:   " << config->genesis_timestamp << "\n"
            << "  genesis hash:   " << blockchain::crypto::to_hex(summary->genesis_hash) << "\n"
            << "  blocks mined:   " << summary->blocks_mined << "\n"
            << "  chain height:   " << summary->height << "\n"
            << "  tip hash:       " << blockchain::crypto::to_hex(summary->tip_hash) << "\n"
            << "  utxo set size:  " << summary->utxo_count << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string program = argc > 0 ? argv[0] : "blockchain_node";
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i) {
      args.emplace_back(argv[i]);
    }
    return run(args, program);
  } catch (const std::exception& ex) {
    std::cerr << "fatal: " << ex.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "fatal: unknown error\n";
    return 1;
  }
}
