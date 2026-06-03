#include "blockchain/validation/block_validation.hpp"

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::validation {
namespace {

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& out) {
  if (lhs > UINT64_MAX - rhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

}  // namespace

Result<void> check_block_sanity(const protocol::Block& block) {
  if (block.transactions.size() > protocol::kMaxTransactionsPerBlock) {
    return make_error(ErrorCode::kInvalidBlock, "too many transactions in block");
  }

  if (block.to_bytes().size() > protocol::kMaxBlockSizeBytes) {
    return make_error(ErrorCode::kInvalidBlock, "block exceeds maximum size");
  }

  if (block.header.merkle_root != protocol::compute_merkle_root(block.transactions)) {
    return make_error(ErrorCode::kInvalidBlock, "merkle root does not match transactions");
  }

  std::set<crypto::Hash256, mempool::Hash256Less> seen;
  for (std::size_t i = 0; i < block.transactions.size(); ++i) {
    const protocol::Transaction& tx = block.transactions[i];
    if (auto sane = check_transaction_sanity(tx); !sane) {
      return sane;
    }
    // A coinbase, if present, must be the first transaction and unique.
    if (tx.is_coinbase() && i != 0) {
      return make_error(ErrorCode::kInvalidBlock, "coinbase must be the first transaction");
    }
    if (!seen.insert(tx.txid()).second) {
      return make_error(ErrorCode::kInvalidBlock, "duplicate transaction in block");
    }
  }

  return {};
}

namespace {

// Records a transaction's outputs as new coins in `working`. `coinbase` marks
// them for maturity tracking. Returns false on a duplicate outpoint.
[[nodiscard]] Result<void> add_outputs(const protocol::Transaction& tx, std::uint32_t height,
                                       bool coinbase, state::UtxoSet& working) {
  const crypto::Hash256 txid = tx.txid();
  for (std::size_t i = 0; i < tx.outputs.size(); ++i) {
    protocol::OutPoint outpoint;
    outpoint.txid = txid;
    outpoint.index = static_cast<std::uint32_t>(i);
    state::Coin coin;
    coin.output = tx.outputs[i];
    coin.height = height;
    coin.coinbase = coinbase;
    if (auto added = working.add(outpoint, coin); !added) {
      return std::unexpected(added.error());
    }
  }
  return {};
}

}  // namespace

Result<std::uint64_t> connect_block(const protocol::Block& block, state::UtxoSet& utxos,
                                    const consensus::ConsensusParams& params) {
  if (auto sane = check_block_sanity(block); !sane) {
    return std::unexpected(sane.error());
  }

  // Work on a copy so a failure leaves the caller's UTXO set untouched.
  state::UtxoSet working = utxos;
  std::uint64_t total_fees = 0;

  const bool has_coinbase = !block.transactions.empty() && block.transactions.front().is_coinbase();

  // First, process every non-coinbase transaction: validate inputs, enforce
  // coinbase maturity, spend inputs, and create outputs. Accumulate fees.
  for (std::size_t i = 0; i < block.transactions.size(); ++i) {
    if (i == 0 && has_coinbase) {
      continue;  // the coinbase is handled after fees are known
    }
    const protocol::Transaction& tx = block.transactions[i];

    auto fee = check_transaction_inputs(tx, working);
    if (!fee) {
      return std::unexpected(fee.error());
    }
    if (auto mature = check_coinbase_maturity(tx, working, block.header.height,
                                              params.coinbase_maturity);
        !mature) {
      return std::unexpected(mature.error());
    }

    for (const protocol::TxInput& input : tx.inputs) {
      if (auto spent = working.spend(input.prevout); !spent) {
        return std::unexpected(spent.error());
      }
    }
    if (auto added = add_outputs(tx, block.header.height, /*coinbase=*/false, working); !added) {
      return std::unexpected(added.error());
    }

    if (!checked_add(total_fees, *fee, total_fees)) {
      return make_error(ErrorCode::kInvalidBlock, "total block fees overflow");
    }
  }

  // Then validate and apply the coinbase: it may pay out at most subsidy + fees.
  if (has_coinbase) {
    const protocol::Transaction& coinbase = block.transactions.front();
    std::uint64_t coinbase_out = 0;
    for (const protocol::TxOutput& output : coinbase.outputs) {
      coinbase_out += output.value;  // bounded by check_transaction_sanity
    }

    std::uint64_t allowed = 0;
    if (!checked_add(params.block_subsidy, total_fees, allowed)) {
      return make_error(ErrorCode::kInvalidBlock, "coinbase reward overflow");
    }
    if (coinbase_out > allowed) {
      return make_error(ErrorCode::kInvalidBlock, "coinbase pays more than subsidy plus fees");
    }
    if (auto added = add_outputs(coinbase, block.header.height, /*coinbase=*/true, working);
        !added) {
      return std::unexpected(added.error());
    }
  }

  utxos = std::move(working);
  return total_fees;
}

}  // namespace blockchain::validation
