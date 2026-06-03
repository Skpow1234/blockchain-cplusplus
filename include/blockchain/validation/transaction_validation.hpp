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
//   * at least one input and one output
//   * input/output counts within protocol limits
//   * no duplicate inputs (same outpoint spent twice)
//   * each output value <= kMaxMoney
//   * sum of outputs does not overflow and stays <= kMaxMoney
//   * serialized size <= kMaxTransactionSizeBytes
//   * coinbase rule: only a coinbase (exactly one input with the reserved
//     sentinel index) may use the null prevout; a non-coinbase must not.
//
// Returns kInvalidTransaction with a descriptive message on failure.
[[nodiscard]] Result<void> check_transaction_sanity(const protocol::Transaction& tx);

// Contextual checks against the current UTXO set (non-coinbase transactions
// only; a coinbase's value is validated at block level):
//   * every input refers to an existing unspent output
//   * sum of input values does not overflow
//   * sum(inputs) >= sum(outputs) (no value creation)
//
// On success returns the transaction fee = sum(inputs) - sum(outputs).
// Assumes check_transaction_sanity has already passed for `tx`.
[[nodiscard]] Result<std::uint64_t> check_transaction_inputs(const protocol::Transaction& tx,
                                                             const state::UtxoSet& utxos);

// Enforces coinbase maturity: every input that spends a coinbase output must be
// at least `maturity` blocks old at `spend_height` (spend_height >= coin.height
// + maturity). Inputs are assumed to exist in `utxos` (validate inputs first).
// Returns kInvalidTransaction for an immature coinbase spend.
[[nodiscard]] Result<void> check_coinbase_maturity(const protocol::Transaction& tx,
                                                   const state::UtxoSet& utxos,
                                                   std::uint32_t spend_height,
                                                   std::uint32_t maturity);

}  // namespace blockchain::validation

#endif  // BLOCKCHAIN_VALIDATION_TRANSACTION_VALIDATION_HPP
