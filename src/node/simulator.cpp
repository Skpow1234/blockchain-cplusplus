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
// local integration runs; override via SimulatorOptions::mempool_limits.
constexpr std::uint32_t kDefaultMempoolMaxTransactions = 10'000;
constexpr std::uint64_t kDefaultMempoolMaxBytes =
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
  params.max_block_size_bytes =
      config.max_block_size_bytes != 0 ? config.max_block_size_bytes : protocol::kMaxBlockSizeBytes;
  params.max_transactions = protocol::kMaxTransactionsPerBlock;
  params.consensus = consensus;
  params.coinbase_recipient = recipient;
  return params;
}

[[nodiscard]] mempool::MempoolLimits resolve_mempool_limits(const SimulatorOptions& options) {
  mempool::MempoolLimits limits = options.mempool_limits;
  if (limits.max_transactions == 0) {
    limits.max_transactions = kDefaultMempoolMaxTransactions;
  }
  if (limits.max_total_bytes == 0) {
    limits.max_total_bytes = kDefaultMempoolMaxBytes;
  }
  return limits;
}

// Deterministic block timestamp for height `h` given the genesis timestamp.
[[nodiscard]] std::uint64_t block_timestamp(std::uint64_t genesis_timestamp, std::uint32_t height) {
  return genesis_timestamp + static_cast<std::uint64_t>(height);
}

[[nodiscard]] bool consensus_params_match(const consensus::ConsensusParams& expected,
                                          const consensus::ConsensusParams& stored) noexcept {
  return expected.block_subsidy == stored.block_subsidy &&
         expected.coinbase_maturity == stored.coinbase_maturity;
}

struct RunStats {
  std::uint32_t blocks_mined = 0;
  std::uint32_t txs_admitted = 0;
  std::uint32_t txs_included = 0;
  std::uint64_t total_fees = 0;
};

[[nodiscard]] Result<void> admit_transactions(mempool::Mempool& pool, const consensus::Chain& chain,
                                              std::span<const protocol::Transaction> txs,
                                              RunStats& stats) {
  for (const protocol::Transaction& tx : txs) {
    if (auto ok = pool.accept(tx, chain.utxos()); !ok) {
      return ok;
    }
    ++stats.txs_admitted;
  }
  return {};
}

[[nodiscard]] Result<void> mine_n_blocks(consensus::Chain& chain, mempool::Mempool& pool,
                                         const NodeConfig& config,
                                         const production::BlockTemplateParams& tmpl_params,
                                         std::uint32_t count, std::vector<protocol::Block>& ledger,
                                         RunStats& stats) {
  for (std::uint32_t n = 0; n < count; ++n) {
    const std::uint64_t timestamp = block_timestamp(config.genesis_timestamp, chain.height() + 1);
    auto tmpl =
        production::build_block_template(chain.tip(), timestamp, pool, chain.utxos(), tmpl_params);
    if (!tmpl) {
      return std::unexpected(tmpl.error());
    }
    auto fees = chain.submit_block(tmpl->block, &pool);
    if (!fees) {
      return std::unexpected(fees.error());
    }
    stats.total_fees += tmpl->total_fees;
    stats.txs_included += static_cast<std::uint32_t>(tmpl->selected_count);
    ledger.push_back(tmpl->block);
    ++stats.blocks_mined;
  }
  return {};
}

[[nodiscard]] SimulatorSummary summary_from_chain(const protocol::Block& genesis,
                                                  const consensus::Chain& chain,
                                                  const RunStats& stats, bool restored) {
  SimulatorSummary summary;
  summary.genesis_hash = genesis.header.hash();
  summary.tip_hash = chain.tip_hash();
  summary.height = chain.height();
  summary.utxo_count = chain.utxos().size();
  summary.blocks_mined = stats.blocks_mined;
  summary.txs_admitted = stats.txs_admitted;
  summary.txs_included = stats.txs_included;
  summary.total_fees = stats.total_fees;
  summary.restored_from_disk = restored;
  return summary;
}

[[nodiscard]] Result<consensus::Chain> replay_loaded_chain(
    std::span<const protocol::Block> blocks, const consensus::ConsensusParams& params) {
  if (blocks.empty()) {
    return make_error(ErrorCode::kStorageCorruption, "ledger contains no blocks");
  }
  auto chain = consensus::Chain::create(blocks.front(), params);
  if (!chain) {
    return std::unexpected(chain.error());
  }
  for (std::size_t i = 1; i < blocks.size(); ++i) {
    if (auto ok = chain->submit_block(blocks[i]); !ok) {
      return std::unexpected(ok.error());
    }
  }
  return *chain;
}

}  // namespace

Result<SimulatorSummary> run_simulator(const NodeConfig& config, const SimulatorOptions& options) {
  const consensus::ConsensusParams consensus = build_consensus_params(config);
  auto recipient = resolve_coinbase_recipient(config);
  if (!recipient) {
    return std::unexpected(recipient.error());
  }

  const protocol::Block genesis = build_genesis_block(config);
  const production::BlockTemplateParams tmpl_params =
      build_template_params(config, consensus, *recipient);
  mempool::Mempool pool(resolve_mempool_limits(options));

  std::vector<protocol::Block> ledger;
  RunStats stats;
  bool restored = false;

  auto chain_result = [&]() -> Result<consensus::Chain> {
    if (!config.restore) {
      ledger.push_back(genesis);
      return consensus::Chain::create(genesis, consensus);
    }

    storage::ChainStore store(config.data_dir);
    if (!store.ledger_exists()) {
      return make_error(ErrorCode::kInvalidConfig, "no ledger found in data_dir for --restore");
    }
    auto loaded = store.load_ledger();
    if (!loaded) {
      return std::unexpected(loaded.error());
    }
    if (!consensus_params_match(consensus, loaded->second)) {
      return make_error(ErrorCode::kStorageCorruption,
                        "restored consensus parameters do not match configuration");
    }
    if (loaded->first.front().header.hash() != genesis.header.hash()) {
      return make_error(ErrorCode::kStorageCorruption,
                        "restored genesis does not match configuration");
    }

    auto replayed = replay_loaded_chain(loaded->first, loaded->second);
    if (!replayed) {
      return std::unexpected(replayed.error());
    }
    ledger = std::move(loaded->first);
    restored = true;
    return *replayed;
  }();
  if (!chain_result) {
    return std::unexpected(chain_result.error());
  }
  consensus::Chain chain = std::move(*chain_result);

  if (!options.steps.empty()) {
    for (const SimulatorStep& step : options.steps) {
      if (auto admitted = admit_transactions(pool, chain, step.submit_txs, stats); !admitted) {
        return std::unexpected(admitted.error());
      }
      if (auto mined =
              mine_n_blocks(chain, pool, config, tmpl_params, step.mine_blocks, ledger, stats);
          !mined) {
        return std::unexpected(mined.error());
      }
    }
  } else if (auto mined =
                 mine_n_blocks(chain, pool, config, tmpl_params, config.mine_blocks, ledger, stats);
             !mined) {
    return std::unexpected(mined.error());
  }

  if (config.persist) {
    storage::ChainStore store(config.data_dir);
    if (auto saved = store.save_ledger(ledger, consensus); !saved) {
      return std::unexpected(saved.error());
    }
  }

  return summary_from_chain(genesis, chain, stats, restored);
}

}  // namespace blockchain::node
