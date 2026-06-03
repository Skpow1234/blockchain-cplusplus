#include "blockchain/consensus/chain.hpp"

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/validation/block_validation.hpp"

namespace blockchain::consensus {
namespace {

// Contextual header-linking rules: everything that depends on the *previous*
// block and therefore cannot live in the context-free validation layer.
[[nodiscard]] Result<void> check_links_to(const protocol::BlockHeader& tip,
                                          const protocol::BlockHeader& next) {
  if (next.version != protocol::kBlockVersion) {
    return make_error(ErrorCode::kUnsupportedVersion, "unsupported block version");
  }
  if (next.prev_block != tip.hash()) {
    return make_error(ErrorCode::kInvalidBlock, "block does not extend the current tip");
  }
  if (tip.height == UINT32_MAX) {
    return make_error(ErrorCode::kInvalidBlock, "chain height would overflow");
  }
  if (next.height != tip.height + 1) {
    return make_error(ErrorCode::kInvalidBlock, "block height does not follow the tip");
  }
  // Monotonic, non-decreasing timestamps. No wall-clock/future check is applied
  // here: consensus-critical logic must stay deterministic and free of
  // environment input.
  if (next.timestamp < tip.timestamp) {
    return make_error(ErrorCode::kInvalidBlock, "block timestamp precedes the tip");
  }
  return {};
}

}  // namespace

Result<Chain> Chain::create(const protocol::Block& genesis, const ConsensusParams& params) {
  if (genesis.header.version != protocol::kBlockVersion) {
    return make_error(ErrorCode::kUnsupportedVersion, "unsupported genesis block version");
  }
  if (genesis.header.height != 0) {
    return make_error(ErrorCode::kInvalidBlock, "genesis block must have height 0");
  }
  if (genesis.header.prev_block != crypto::zero_hash()) {
    return make_error(ErrorCode::kInvalidBlock, "genesis block must reference the zero hash");
  }
  if (auto sane = validation::check_block_sanity(genesis); !sane) {
    return std::unexpected<Error>(sane.error());
  }

  state::UtxoSet utxos;
  if (auto connected = validation::connect_block(genesis, utxos, params); !connected) {
    return std::unexpected<Error>(connected.error());
  }
  return Chain(genesis.header, std::move(utxos), params);
}

Result<std::uint64_t> Chain::submit_block(const protocol::Block& block, mempool::Mempool* mempool) {
  if (auto linked = check_links_to(tip_, block.header); !linked) {
    return std::unexpected<Error>(linked.error());
  }
  if (auto sane = validation::check_block_sanity(block); !sane) {
    return std::unexpected<Error>(sane.error());
  }

  // connect_block is all-or-nothing: utxos_ is only mutated on full success.
  auto fees = validation::connect_block(block, utxos_, params_);
  if (!fees) {
    return std::unexpected<Error>(fees.error());
  }

  tip_ = block.header;

  // Block fully connected: drop every included transaction from the mempool so
  // it is not reconsidered for the next template.
  if (mempool != nullptr) {
    for (const protocol::Transaction& tx : block.transactions) {
      mempool->remove(tx.txid());
    }
  }
  return *fees;
}

}  // namespace blockchain::consensus
