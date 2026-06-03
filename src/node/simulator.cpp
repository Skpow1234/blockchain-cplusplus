#include "blockchain/node/simulator.hpp"

#include "blockchain/consensus/chain.hpp"
#include "blockchain/consensus/params.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/storage/chain_store.hpp"

namespace blockchain::node {
namespace {

// Stage-1 simulator mempool capacity (not consensus constants). Large enough for
// local integration runs; configurable file-based limits are a planned follow-up.
constexpr std::uint32_t kSimulatorMempoolMaxTransactions = 10'000;
constexpr std::uint64_t kSimulatorMempoolMaxBytes =
    100ULL * static_cast<std::uint64_t>(protocol::kMaxBlockSizeBytes);

[[nodiscard]] consensus::ConsensusParams build_consensus_params(const NodeConfig& config) {
  consensus::ConsensusParams params;
  if (config.block_subsidy != 0) {
    params.block_subsidy = config.block_subsidy;
  }
  if (config.coinbase_maturity != 0) {
    params.coinbase_maturity = config.coinbase_maturity;
  }
  return params;
}

[[nodiscard]] Result<crypto::Hash256> resolve_coinbase_recipient(const NodeConfig& config) {
  if (config.coinbase_recipient_hex.empty()) {
    return crypto::zero_hash();
  }
  return crypto::hash_from_hex(config.coinbase_recipient_hex);
}

[[nodiscard]] production::BlockTemplateParams build_template_params(
    const NodeConfig& config, const consensus::ConsensusParams& consensus,
    const crypto::Hash256& recipient) {
  production::BlockTemplateParams params;
  params.version = protocol::kBlockVersion;
  params.max_block_size_bytes = config.max_block_size_bytes != 0 ? config.max_block_size_bytes
                                                                 : protocol::kMaxBlockSizeBytes;
  params.max_transactions = protocol::kMaxTransactionsPerBlock;
  params.consensus = consensus;
  params.coinbase_recipient = recipient;
  return params;
}

// Deterministic block timestamp for height `h` given the genesis timestamp.
[[nodiscard]] std::uint64_t block_timestamp(std::uint64_t genesis_timestamp, std::uint32_t height) {
  return genesis_timestamp + static_cast<std::uint64_t>(height);
}

[[nodiscard]] SimulatorSummary summary_from_chain(const protocol::Block& genesis,
                                                  const consensus::Chain& chain,
                                                  std::uint32_t blocks_mined, bool restored) {
  SimulatorSummary summary;
  summary.genesis_hash = genesis.header.hash();
  summary.tip_hash = chain.tip_hash();
  summary.height = chain.height();
  summary.utxo_count = chain.utxos().size();
  summary.blocks_mined = blocks_mined;
  summary.restored_from_disk = restored;
  return summary;
}

}  // namespace

Result<SimulatorSummary> run_simulator(const NodeConfig& config) {
  const consensus::ConsensusParams consensus = build_consensus_params(config);
  auto recipient = resolve_coinbase_recipient(config);
  if (!recipient) {
    return std::unexpected(recipient.error());
  }

  const protocol::Block genesis = build_genesis_block(config);

  if (config.restore) {
    storage::ChainStore store(config.data_dir);
    if (!store.ledger_exists()) {
      return make_error(ErrorCode::kInvalidConfig, "no ledger found in data_dir for --restore");
    }
    auto chain = store.load_chain();
    if (!chain) {
      return std::unexpected(chain.error());
    }
    return summary_from_chain(genesis, *chain, 0, true);
  }
  auto chain = consensus::Chain::create(genesis, consensus);
  if (!chain) {
    return std::unexpected(chain.error());
  }

  mempool::Mempool pool(mempool::MempoolLimits{.max_transactions = kSimulatorMempoolMaxTransactions,
                                               .max_total_bytes = kSimulatorMempoolMaxBytes});
  const production::BlockTemplateParams tmpl_params =
      build_template_params(config, consensus, *recipient);

  std::vector<protocol::Block> ledger;
  ledger.push_back(genesis);

  for (std::uint32_t n = 0; n < config.mine_blocks; ++n) {
    const std::uint64_t timestamp =
        block_timestamp(config.genesis_timestamp, chain->height() + 1);
    auto tmpl = production::build_block_template(chain->tip(), timestamp, pool, chain->utxos(),
                                                 tmpl_params);
    if (!tmpl) {
      return std::unexpected(tmpl.error());
    }
    auto fees = chain->submit_block(tmpl->block, &pool);
    if (!fees) {
      return std::unexpected(fees.error());
    }
    ledger.push_back(tmpl->block);
  }

  if (config.persist) {
    storage::ChainStore store(config.data_dir);
    if (auto saved = store.save_ledger(ledger, consensus); !saved) {
      return std::unexpected(saved.error());
    }
  }

  return summary_from_chain(genesis, *chain, config.mine_blocks, false);
}

}  // namespace blockchain::node
