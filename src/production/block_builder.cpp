#include "blockchain/production/block_builder.hpp"

#include <cstdint>
#include <utility>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/validation/block_validation.hpp"
#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::production {
namespace {

[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& out) {
  if (lhs > UINT64_MAX - rhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

// Attempts to apply a transaction's state transition to a copy of `working`.
// Returns the updated set on success, or std::nullopt if any step fails (e.g.
// an output outpoint already exists). Leaves `working` untouched on failure.
[[nodiscard]] bool try_apply(const protocol::Transaction& tx, std::uint32_t height,
                             state::UtxoSet& working) {
  state::UtxoSet trial = working;
  for (const protocol::TxInput& input : tx.inputs) {
    if (!trial.spend(input.prevout)) {
      return false;
    }
  }
  const crypto::Hash256 txid = tx.txid();
  for (std::size_t i = 0; i < tx.outputs.size(); ++i) {
    protocol::OutPoint outpoint;
    outpoint.txid = txid;
    outpoint.index = static_cast<std::uint32_t>(i);
    state::Coin coin;
    coin.output = tx.outputs[i];
    coin.height = height;
    coin.coinbase = false;
    if (!trial.add(outpoint, coin)) {
      return false;
    }
  }
  working = std::move(trial);
  return true;
}

}  // namespace

Result<BlockTemplate> build_block_template(const protocol::BlockHeader& tip,
                                           std::uint64_t timestamp, const mempool::Mempool& mempool,
                                           const state::UtxoSet& utxos,
                                           const BlockTemplateParams& params) {
  if (tip.height == UINT32_MAX) {
    return make_error(ErrorCode::kInvalidBlock, "chain height would overflow");
  }

  protocol::Block block;
  block.header.version = params.version;
  block.header.prev_block = tip.hash();
  block.header.height = tip.height + 1;
  block.header.timestamp = timestamp;

  // Base size: serialized header plus the (empty) transaction-count prefix.
  block.header.merkle_root = crypto::zero_hash();
  std::size_t current_size = block.to_bytes().size();

  state::UtxoSet working = utxos;
  std::uint64_t total_fees = 0;

  for (const mempool::Mempool::Entry& entry : mempool.entries_by_feerate()) {
    if (block.transactions.size() >= params.max_transactions) {
      break;
    }
    const std::size_t projected = current_size + static_cast<std::size_t>(entry.size_bytes);
    if (projected > params.max_block_size_bytes) {
      continue;  // a smaller transaction later may still fit
    }

    auto fee = validation::check_transaction_inputs(entry.transaction, working);
    if (!fee) {
      continue;  // not applicable against current candidate state
    }
    if (!try_apply(entry.transaction, block.header.height, working)) {
      continue;
    }

    std::uint64_t new_fees = 0;
    if (!checked_add(total_fees, *fee, new_fees)) {
      continue;  // skip a transaction that would overflow the fee total
    }
    total_fees = new_fees;
    current_size = projected;
    block.transactions.push_back(entry.transaction);
  }

  block.header.merkle_root = protocol::compute_merkle_root(block.transactions);

  // Producer invariant: the candidate must validate against the real UTXO set.
  state::UtxoSet verify = utxos;
  auto connected = validation::connect_block(block, verify);
  if (!connected) {
    return make_error(ErrorCode::kInvalidBlock,
                      "produced block failed validation: " + connected.error().message);
  }

  BlockTemplate result;
  result.total_fees = *connected;
  result.selected_count = block.transactions.size();
  result.block = std::move(block);
  return result;
}

}  // namespace blockchain::production
