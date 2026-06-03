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
    const bool relay = config->network_mode == blockchain::node::NetworkMode::kRelay;
    if (blockchain::node::is_network_server(*config)) {
      if (relay) {
        auto ok = blockchain::node::run_relay_server(*config);
        if (!ok) {
          std::cerr << "network error: " << ok.error().message << "\n";
          return 1;
        }
        std::cout << "relay server completed on " << config->listen_host << ":" << ok->listen_port;
        if (config->restore) {
          std::cout << " (restored from " << config->data_dir << ")";
        }
        std::cout << "\n";
        if (ok->sessions_completed > 1) {
          std::cout << "  sessions served: " << ok->sessions_completed << "\n";
        }
        std::cout << "  chain height:   " << ok->last_session.height << "\n";
      } else {
        auto ok = blockchain::node::run_ping_server(*config);
        if (!ok) {
          std::cerr << "network error: " << ok.error().message << "\n";
          return 1;
        }
        std::cout << "ping server completed on " << config->listen_host;
        if (config->listen_port != 0) {
          std::cout << ":" << config->listen_port;
        }
        std::cout << "\n";
      }
      return 0;
    }
    if (relay) {
      auto ok = blockchain::node::run_relay_client(*config);
      if (!ok) {
        std::cerr << "network error: " << ok.error().message << "\n";
        return 1;
      }
      std::cout << "relay client completed (" << config->peer_host << ":" << config->peer_port
                << ")\n"
                << "  chain height:   " << ok->height << "\n"
                << "  tip hash:       " << blockchain::crypto::to_hex(ok->tip_hash) << "\n";
    } else {
      auto ok = blockchain::node::run_ping_client(*config);
      if (!ok) {
        std::cerr << "network error: " << ok.error().message << "\n";
        return 1;
      }
      std::cout << "ping client completed (" << config->peer_host << ":" << config->peer_port
                << ")\n";
    }
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
            << "  restored:       " << (summary->restored_from_disk ? "yes" : "no") << "\n"
            << "  genesis time:   " << config->genesis_timestamp << "\n"
            << "  genesis hash:   " << blockchain::crypto::to_hex(summary->genesis_hash) << "\n"
            << "  blocks mined:   " << summary->blocks_mined << "\n"
            << "  chain height:   " << summary->height << "\n"
            << "  tip hash:       " << blockchain::crypto::to_hex(summary->tip_hash) << "\n"
            << "  utxo set size:  " << summary->utxo_count << "\n"
            << "  txs admitted:   " << summary->txs_admitted << "\n"
            << "  txs included:   " << summary->txs_included << "\n"
            << "  total fees:     " << summary->total_fees << "\n";
  if (config->persist) {
    std::cout << "  ledger saved:   " << config->data_dir << "/ledger.bin\n";
  }
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
