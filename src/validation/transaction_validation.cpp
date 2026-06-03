#include "blockchain/validation/transaction_validation.hpp"

#include <cstdint>
#include <set>
#include <string>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"

namespace blockchain::validation {
namespace {

// Adds two u64 values, reporting overflow rather than wrapping. Used for money
// sums so that a crafted transaction cannot bypass value checks via wraparound.
[[nodiscard]] bool checked_add(std::uint64_t lhs, std::uint64_t rhs, std::uint64_t& out) {
  if (lhs > UINT64_MAX - rhs) {
    return false;
  }
  out = lhs + rhs;
  return true;
}

[[nodiscard]] std::string outpoint_str(const protocol::OutPoint& outpoint) {
  return crypto::to_hex(outpoint.txid) + ":" + std::to_string(outpoint.index);
}

}  // namespace

Result<void> check_transaction_sanity(const protocol::Transaction& tx) {
  if (tx.inputs.empty()) {
    return make_error(ErrorCode::kInvalidTransaction, "transaction has no inputs");
  }
  if (tx.outputs.empty()) {
    return make_error(ErrorCode::kInvalidTransaction, "transaction has no outputs");
  }
  if (tx.inputs.size() > protocol::kMaxInputsPerTransaction) {
    return make_error(ErrorCode::kInvalidTransaction, "too many inputs");
  }
  if (tx.outputs.size() > protocol::kMaxOutputsPerTransaction) {
    return make_error(ErrorCode::kInvalidTransaction, "too many outputs");
  }

  // Only a well-formed coinbase may use the reserved null-prevout sentinel. Any
  // other transaction that references it is malformed.
  const bool coinbase = tx.is_coinbase();
  if (!coinbase) {
    for (const protocol::TxInput& input : tx.inputs) {
      if (input.prevout.index == protocol::kNullPrevoutIndex) {
        return make_error(ErrorCode::kInvalidTransaction,
                          "non-coinbase input uses the reserved coinbase outpoint");
      }
    }
  }

  // No duplicate inputs: spending the same outpoint twice in one transaction is
  // invalid regardless of chain state. (A coinbase has a single input.)
  std::set<protocol::OutPoint, state::OutPointLess> seen;
  for (const protocol::TxInput& input : tx.inputs) {
    if (!seen.insert(input.prevout).second) {
      return make_error(ErrorCode::kInvalidTransaction,
                        "duplicate input " + outpoint_str(input.prevout));
    }
  }

  // Output value range and overflow-safe sum.
  std::uint64_t total_out = 0;
  for (const protocol::TxOutput& output : tx.outputs) {
    if (output.value > protocol::kMaxMoney) {
      return make_error(ErrorCode::kInvalidTransaction, "output value exceeds maximum");
    }
    if (!checked_add(total_out, output.value, total_out) || total_out > protocol::kMaxMoney) {
      return make_error(ErrorCode::kInvalidTransaction, "sum of outputs exceeds maximum");
    }
  }

  if (tx.to_bytes().size() > protocol::kMaxTransactionSizeBytes) {
    return make_error(ErrorCode::kInvalidTransaction, "transaction exceeds maximum size");
  }

  return {};
}

Result<std::uint64_t> check_transaction_inputs(const protocol::Transaction& tx,
                                               const state::UtxoSet& utxos) {
  std::uint64_t total_in = 0;
  for (const protocol::TxInput& input : tx.inputs) {
    const state::Coin* coin = utxos.find(input.prevout);
    if (coin == nullptr) {
      return make_error(ErrorCode::kInvalidTransaction,
                        "input refers to missing or spent output " + outpoint_str(input.prevout));
    }
    if (!checked_add(total_in, coin->output.value, total_in)) {
      return make_error(ErrorCode::kInvalidTransaction, "sum of inputs overflows");
    }
  }

  std::uint64_t total_out = 0;
  for (const protocol::TxOutput& output : tx.outputs) {
    total_out += output.value;  // bounded by check_transaction_sanity
  }

  if (total_in < total_out) {
    return make_error(ErrorCode::kInvalidTransaction, "outputs (" + std::to_string(total_out) +
                                                          ") exceed inputs (" +
                                                          std::to_string(total_in) + ")");
  }

  return total_in - total_out;
}

Result<void> check_coinbase_maturity(const protocol::Transaction& tx, const state::UtxoSet& utxos,
                                     std::uint32_t spend_height, std::uint32_t maturity) {
  for (const protocol::TxInput& input : tx.inputs) {
    const state::Coin* coin = utxos.find(input.prevout);
    if (coin == nullptr || !coin->coinbase) {
      continue;
    }
    // Compare in 64 bits so coin.height + maturity cannot overflow u32.
    const std::uint64_t spendable_at =
        static_cast<std::uint64_t>(coin->height) + static_cast<std::uint64_t>(maturity);
    if (static_cast<std::uint64_t>(spend_height) < spendable_at) {
      return make_error(ErrorCode::kInvalidTransaction,
                        "immature coinbase spend of " + outpoint_str(input.prevout));
    }
  }
  return {};
}

}  // namespace blockchain::validation
