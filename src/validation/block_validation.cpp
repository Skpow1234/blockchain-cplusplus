#include "blockchain/validation/block_validation.hpp"

#include <cstdint>
#include <set>
#include <string>
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
  for (const protocol::Transaction& tx : block.transactions) {
    if (auto sane = check_transaction_sanity(tx); !sane) {
      return sane;
    }
    if (!seen.insert(tx.txid()).second) {
      return make_error(ErrorCode::kInvalidBlock, "duplicate transaction in block");
    }
  }

  return {};
}

Result<std::uint64_t> connect_block(const protocol::Block& block, state::UtxoSet& utxos) {
  if (auto sane = check_block_sanity(block); !sane) {
    return std::unexpected(sane.error());
  }

  // Work on a copy so a failure leaves the caller's UTXO set untouched.
  state::UtxoSet working = utxos;
  std::uint64_t total_fees = 0;

  for (const protocol::Transaction& tx : block.transactions) {
    auto fee = check_transaction_inputs(tx, working);
    if (!fee) {
      return std::unexpected(fee.error());
    }

    for (const protocol::TxInput& input : tx.inputs) {
      if (auto spent = working.spend(input.prevout); !spent) {
        return std::unexpected(spent.error());
      }
    }

    const crypto::Hash256 txid = tx.txid();
    for (std::size_t i = 0; i < tx.outputs.size(); ++i) {
      protocol::OutPoint outpoint;
      outpoint.txid = txid;
      outpoint.index = static_cast<std::uint32_t>(i);
      state::Coin coin;
      coin.output = tx.outputs[i];
      coin.height = block.header.height;
      coin.coinbase = false;
      if (auto added = working.add(outpoint, coin); !added) {
        return std::unexpected(added.error());
      }
    }

    if (!checked_add(total_fees, *fee, total_fees)) {
      return make_error(ErrorCode::kInvalidBlock, "total block fees overflow");
    }
  }

  utxos = std::move(working);
  return total_fees;
}

}  // namespace blockchain::validation
