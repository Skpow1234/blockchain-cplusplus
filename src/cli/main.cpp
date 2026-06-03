#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/protocol/block.hpp"

namespace {

int run(const std::vector<std::string>& args, std::string_view program) {
  auto config = blockchain::node::parse_args(args);
  if (!config) {
    // --help is reported as a handled "error"; treat it as success.
    if (config.error().message == "help requested") {
      std::cout << blockchain::node::usage(program);
      return 0;
    }
    std::cerr << "configuration error: " << config.error().message << "\n\n"
              << blockchain::node::usage(program);
    return 2;
  }

  const blockchain::protocol::Block genesis = blockchain::node::build_genesis_block(*config);

  std::cout << "blockchain simulator (stage 1)\n"
            << "  node id:       " << config->node_id << "\n"
            << "  data dir:      " << config->data_dir << "\n"
            << "  log level:     " << blockchain::node::to_string(config->log_level) << "\n"
            << "  genesis time:  " << config->genesis_timestamp << "\n"
            << "  genesis hash:  " << blockchain::crypto::to_hex(genesis.header.hash()) << "\n"
            << "  genesis txs:   " << genesis.transactions.size() << "\n";
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
