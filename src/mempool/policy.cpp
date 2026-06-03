#include "blockchain/mempool/policy.hpp"

#include <cstdint>

#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::mempool {
namespace {

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

// True when lhs >= rhs for unsigned 128-bit values (hi, lo pairs).
bool u128_gte(std::uint64_t lhs_hi, std::uint64_t lhs_lo, std::uint64_t rhs_hi,
              std::uint64_t rhs_lo) {
  if (lhs_hi != rhs_hi) {
    return lhs_hi > rhs_hi;
  }
  return lhs_lo >= rhs_lo;
}

[[nodiscard]] Result<void> check_min_relay_feerate(std::uint64_t fee, std::uint64_t size_bytes,
                                                  const MempoolPolicy& policy) {
  if (policy.min_relay_feerate == 0 || size_bytes == 0) {
    return {};
  }

  std::uint64_t required_hi = 0;
  std::uint64_t required_lo = 0;
  mul_u64(policy.min_relay_feerate, size_bytes, required_hi, required_lo);

  std::uint64_t fee_hi = 0;
  std::uint64_t fee_lo = fee;
  if (!u128_gte(fee_hi, fee_lo, required_hi, required_lo)) {
    return make_error(ErrorCode::kPolicyRejected, "transaction fee rate below minimum relay feerate");
  }
  return {};
}

}  // namespace

Result<MempoolAdmission> validate_for_mempool(const protocol::Transaction& tx,
                                              const state::UtxoSet& utxos,
                                              const MempoolPolicy& policy) {
  if (auto sane = validation::check_transaction_sanity(tx); !sane) {
    return std::unexpected(sane.error());
  }

  if (tx.is_coinbase()) {
    return make_error(ErrorCode::kInvalidTransaction, "coinbase cannot enter the mempool");
  }

  auto fee = validation::check_transaction_inputs(tx, utxos);
  if (!fee) {
    return std::unexpected(fee.error());
  }

  const std::uint64_t size_bytes = static_cast<std::uint64_t>(tx.to_bytes().size());
  if (auto policy_ok = check_min_relay_feerate(*fee, size_bytes, policy); !policy_ok) {
    return std::unexpected(policy_ok.error());
  }

  return MempoolAdmission{.fee = *fee, .size_bytes = size_bytes};
}

}  // namespace blockchain::mempool
