#include "blockchain/mempool/mempool.hpp"

#include <cstdint>
#include <vector>

#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::mempool {
namespace {

// Full 128-bit product of two u64 values, returned as (high, low). Used to
// compare fee rates (fee/size) without floating point and without overflow, so
// ordering is deterministic across platforms and toolchains.
void mul_u64(std::uint64_t x, std::uint64_t y, std::uint64_t& hi, std::uint64_t& lo) {
  constexpr std::uint64_t kMask = 0xffffffffULL;
  const std::uint64_t x0 = x & kMask;
  const std::uint64_t x1 = x >> 32U;
  const std::uint64_t y0 = y & kMask;
  const std::uint64_t y1 = y >> 32U;

  const std::uint64_t p00 = x0 * y0;
  const std::uint64_t p01 = x0 * y1;
  const std::uint64_t p10 = x1 * y0;
  const std::uint64_t p11 = x1 * y1;

  const std::uint64_t mid = (p00 >> 32U) + (p01 & kMask) + (p10 & kMask);
  lo = (p00 & kMask) | (mid << 32U);
  hi = p11 + (p01 >> 32U) + (p10 >> 32U) + (mid >> 32U);
}

// True if entry `a` should be ordered before entry `b`: strictly higher fee
// rate, with ties broken by ascending txid. Compares a.fee/a.size vs
// b.fee/b.size via cross-multiplication (a.fee*b.size vs b.fee*a.size).
bool higher_feerate(const Mempool::Entry& a, const Mempool::Entry& b) {
  std::uint64_t lhs_hi = 0;
  std::uint64_t lhs_lo = 0;
  std::uint64_t rhs_hi = 0;
  std::uint64_t rhs_lo = 0;
  mul_u64(a.fee, b.size_bytes, lhs_hi, lhs_lo);
  mul_u64(b.fee, a.size_bytes, rhs_hi, rhs_lo);

  if (lhs_hi != rhs_hi) {
    return lhs_hi > rhs_hi;
  }
  if (lhs_lo != rhs_lo) {
    return lhs_lo > rhs_lo;
  }
  return Hash256Less{}(a.txid, b.txid);
}

}  // namespace

Result<void> Mempool::accept(const protocol::Transaction& tx, const state::UtxoSet& utxos) {
  if (auto sane = validation::check_transaction_sanity(tx); !sane) {
    return sane;
  }

  // Coinbase transactions are created by block producers, never relayed or
  // pooled; they have no real inputs to validate here.
  if (tx.is_coinbase()) {
    return make_error(ErrorCode::kInvalidTransaction, "coinbase cannot enter the mempool");
  }

  const crypto::Hash256 txid = tx.txid();
  if (by_txid_.find(txid) != by_txid_.end()) {
    return make_error(ErrorCode::kInvalidTransaction, "transaction already in mempool");
  }

  auto fee = validation::check_transaction_inputs(tx, utxos);
  if (!fee) {
    return std::unexpected(fee.error());
  }

  // Conflict detection: reject if any input is already reserved by another
  // in-mempool transaction (no replacement policy yet).
  for (const protocol::TxInput& input : tx.inputs) {
    if (spent_by_.find(input.prevout) != spent_by_.end()) {
      return make_error(ErrorCode::kInvalidTransaction,
                        "input conflicts with an in-mempool transaction");
    }
  }

  const std::uint64_t size_bytes = static_cast<std::uint64_t>(tx.to_bytes().size());

  if (by_txid_.size() + 1 > limits_.max_transactions) {
    return make_error(ErrorCode::kResourceLimitExceeded, "mempool transaction count limit reached");
  }
  // Guard the subtraction against unsigned underflow before comparing.
  if (size_bytes > limits_.max_total_bytes || total_bytes_ > limits_.max_total_bytes - size_bytes) {
    return make_error(ErrorCode::kResourceLimitExceeded, "mempool byte limit reached");
  }

  Entry entry;
  entry.transaction = tx;
  entry.txid = txid;
  entry.fee = *fee;
  entry.size_bytes = size_bytes;

  for (const protocol::TxInput& input : tx.inputs) {
    spent_by_.emplace(input.prevout, txid);
  }
  by_txid_.emplace(txid, std::move(entry));
  total_bytes_ += size_bytes;

  return {};
}

bool Mempool::contains(const crypto::Hash256& txid) const {
  return by_txid_.find(txid) != by_txid_.end();
}

const Mempool::Entry* Mempool::find(const crypto::Hash256& txid) const {
  const auto it = by_txid_.find(txid);
  return it == by_txid_.end() ? nullptr : &it->second;
}

void Mempool::remove(const crypto::Hash256& txid) {
  const auto it = by_txid_.find(txid);
  if (it == by_txid_.end()) {
    return;
  }
  for (const protocol::TxInput& input : it->second.transaction.inputs) {
    spent_by_.erase(input.prevout);
  }
  total_bytes_ -= it->second.size_bytes;
  by_txid_.erase(it);
}

std::vector<Mempool::Entry> Mempool::entries_by_feerate() const {
  std::vector<Entry> out;
  out.reserve(by_txid_.size());
  for (const auto& [txid, entry] : by_txid_) {
    out.push_back(entry);
  }
  std::sort(out.begin(), out.end(), higher_feerate);
  return out;
}

}  // namespace blockchain::mempool
