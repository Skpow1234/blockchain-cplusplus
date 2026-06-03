#ifndef BLOCKCHAIN_VALIDATION_TRANSACTION_VALIDATION_HPP
#define BLOCKCHAIN_VALIDATION_TRANSACTION_VALIDATION_HPP

#include <cstdint>

#include "blockchain/error.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"

// Consensus-level transaction validation.
//
// This module enforces *consensus* validity only. Mempool acceptance policy,
// relay policy, and local node policy are deliberately kept separate (a
// transaction can be consensus-valid yet rejected by policy, and vice versa is
// never allowed). All checks here are deterministic and free of wall-clock,
// networking, and configuration dependencies.
namespace blockchain::validation {

// Context-free checks that do not require any chain state:
//   * at least one input and one output (coinbase is handled by block rules)
//   * input/output counts within protocol limits
//   * no duplicate inputs (same outpoint spent twice)
//   * each output value <= kMaxMoney
//   * sum of outputs does not overflow and stays <= kMaxMoney
//   * serialized size <= kMaxTransactionSizeBytes
//
// Returns kInvalidTransaction with a descriptive message on failure.
[[nodiscard]] Result<void> check_transaction_sanity(const protocol::Transaction& tx);

// Contextual checks against the current UTXO set:
//   * every input refers to an existing unspent output
//   * sum of input values does not overflow
//   * sum(inputs) >= sum(outputs) (no value creation)
//
// On success returns the transaction fee = sum(inputs) - sum(outputs).
// Assumes check_transaction_sanity has already passed for `tx`.
[[nodiscard]] Result<std::uint64_t> check_transaction_inputs(const protocol::Transaction& tx,
                                                             const state::UtxoSet& utxos);

}  // namespace blockchain::validation

#endif  // BLOCKCHAIN_VALIDATION_TRANSACTION_VALIDATION_HPP
