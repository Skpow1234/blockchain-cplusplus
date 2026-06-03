#include <cstdint>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/mempool/policy.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::mempool::MempoolPolicy;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;

namespace {

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

// Builds a transaction spending (tag,index) and creating a single output of
// `out_value` to `recipient_tag`.
Transaction spend_tx(std::uint8_t tag, std::uint32_t index, std::uint64_t out_value,
                     std::uint8_t recipient_tag) {
  Transaction tx;
  TxInput input;
  input.prevout = make_outpoint(tag, index);
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = out_value;
  output.recipient.fill(std::byte{recipient_tag});
  tx.outputs.push_back(output);
  return tx;
}

UtxoSet funded(std::uint8_t tag, std::uint32_t index, std::uint64_t value) {
  UtxoSet set;
  Coin coin;
  coin.output.value = value;
  (void)set.add(make_outpoint(tag, index), coin);
  return set;
}

MempoolLimits generous() {
  return MempoolLimits{.max_transactions = 1000, .max_total_bytes = 1U << 20U};
}

}  // namespace

TEST_CASE("accept admits a valid transaction and records its fee") {
  Mempool pool(generous());
  const UtxoSet set = funded(0x01, 0, 100);
  const Transaction tx = spend_tx(0x01, 0, 70, 0xAA);

  CHECK(pool.accept(tx, set).has_value());
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
  CHECK(pool.contains(tx.txid()));
  const Mempool::Entry* entry = pool.find(tx.txid());
  CHECK(entry != nullptr);
  CHECK_EQ(entry->fee, static_cast<std::uint64_t>(30));
}

TEST_CASE("duplicate transaction is rejected") {
  Mempool pool(generous());
  const UtxoSet set = funded(0x02, 0, 100);
  const Transaction tx = spend_tx(0x02, 0, 70, 0xAA);

  CHECK(pool.accept(tx, set).has_value());
  auto again = pool.accept(tx, set);
  CHECK(!again.has_value());
  CHECK(again.error().code == ErrorCode::kInvalidTransaction);
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
}

TEST_CASE("conflicting transaction (double spend) is rejected") {
  Mempool pool(generous());
  const UtxoSet set = funded(0x03, 0, 100);
  const Transaction a = spend_tx(0x03, 0, 70, 0xAA);
  const Transaction b = spend_tx(0x03, 0, 60, 0xBB);  // spends same outpoint

  CHECK(pool.accept(a, set).has_value());
  auto conflict = pool.accept(b, set);
  CHECK(!conflict.has_value());
  CHECK(conflict.error().code == ErrorCode::kInvalidTransaction);
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
}

TEST_CASE("transaction with missing inputs is rejected") {
  Mempool pool(generous());
  const UtxoSet empty;
  const Transaction tx = spend_tx(0x04, 0, 10, 0xAA);
  auto result = pool.accept(tx, empty);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("count limit is enforced") {
  Mempool pool(MempoolLimits{.max_transactions = 1, .max_total_bytes = 1U << 20U});
  UtxoSet set;
  (void)set.add(make_outpoint(0x05, 0), Coin{.output = {.value = 100, .recipient = {}}});
  (void)set.add(make_outpoint(0x06, 0), Coin{.output = {.value = 100, .recipient = {}}});

  CHECK(pool.accept(spend_tx(0x05, 0, 70, 0xAA), set).has_value());
  auto over = pool.accept(spend_tx(0x06, 0, 70, 0xAA), set);
  CHECK(!over.has_value());
  CHECK(over.error().code == ErrorCode::kResourceLimitExceeded);
}

TEST_CASE("byte limit is enforced") {
  Mempool pool(MempoolLimits{.max_transactions = 1000, .max_total_bytes = 1});
  const UtxoSet set = funded(0x07, 0, 100);
  auto over = pool.accept(spend_tx(0x07, 0, 70, 0xAA), set);
  CHECK(!over.has_value());
  CHECK(over.error().code == ErrorCode::kResourceLimitExceeded);
}

TEST_CASE("remove frees the outpoint so a conflict can later be accepted") {
  Mempool pool(generous());
  const UtxoSet set = funded(0x08, 0, 100);
  const Transaction a = spend_tx(0x08, 0, 70, 0xAA);
  const Transaction b = spend_tx(0x08, 0, 60, 0xBB);

  CHECK(pool.accept(a, set).has_value());
  pool.remove(a.txid());
  CHECK(!pool.contains(a.txid()));
  CHECK_EQ(pool.total_bytes(), static_cast<std::uint64_t>(0));
  CHECK(pool.accept(b, set).has_value());
}

TEST_CASE("entries_by_feerate orders by descending fee rate") {
  Mempool pool(generous());
  UtxoSet set;
  (void)set.add(make_outpoint(0x10, 0), Coin{.output = {.value = 1000, .recipient = {}}});
  (void)set.add(make_outpoint(0x11, 0), Coin{.output = {.value = 1000, .recipient = {}}});

  // Same size (1-in/1-out); low fee vs high fee.
  const Transaction low = spend_tx(0x10, 0, 990, 0xAA);   // fee 10
  const Transaction high = spend_tx(0x11, 0, 100, 0xBB);  // fee 900
  CHECK(pool.accept(low, set).has_value());
  CHECK(pool.accept(high, set).has_value());

  const std::vector<Mempool::Entry> ordered = pool.entries_by_feerate();
  CHECK_EQ(ordered.size(), static_cast<std::size_t>(2));
  CHECK(ordered.front().txid == high.txid());
  CHECK(ordered.back().txid == low.txid());
}

TEST_CASE("min relay feerate rejects consensus-valid low-fee transaction") {
  Mempool pool(generous());
  const UtxoSet set = funded(0x20, 0, 100);
  const Transaction tx = spend_tx(0x20, 0, 99, 0xAA);  // fee 1

  MempoolPolicy policy{.min_relay_feerate = 2};
  auto result = pool.accept(tx, set, policy);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kPolicyRejected);
  CHECK_EQ(pool.size(), static_cast<std::size_t>(0));
}

TEST_CASE("higher feerate transaction evicts lower feerate when count limit reached") {
  Mempool pool(MempoolLimits{.max_transactions = 1, .max_total_bytes = 1U << 20U});
  UtxoSet set;
  (void)set.add(make_outpoint(0x21, 0), Coin{.output = {.value = 1000, .recipient = {}}});
  (void)set.add(make_outpoint(0x22, 0), Coin{.output = {.value = 1000, .recipient = {}}});

  const Transaction low = spend_tx(0x21, 0, 990, 0xAA);   // fee 10
  const Transaction high = spend_tx(0x22, 0, 100, 0xBB);  // fee 900
  CHECK(pool.accept(low, set).has_value());
  CHECK(pool.contains(low.txid()));

  CHECK(pool.accept(high, set).has_value());
  CHECK(!pool.contains(low.txid()));
  CHECK(pool.contains(high.txid()));
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
}

TEST_CASE("equal feerate cannot evict when count limit reached") {
  Mempool pool(MempoolLimits{.max_transactions = 1, .max_total_bytes = 1U << 20U});
  UtxoSet set;
  (void)set.add(make_outpoint(0x23, 0), Coin{.output = {.value = 100, .recipient = {}}});
  (void)set.add(make_outpoint(0x24, 0), Coin{.output = {.value = 100, .recipient = {}}});

  const Transaction first = spend_tx(0x23, 0, 70, 0xAA);
  const Transaction second = spend_tx(0x24, 0, 70, 0xBB);
  CHECK(pool.accept(first, set).has_value());
  auto over = pool.accept(second, set);
  CHECK(!over.has_value());
  CHECK(over.error().code == ErrorCode::kResourceLimitExceeded);
  CHECK(pool.contains(first.txid()));
}
