#include <cstdint>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::protocol::OutPoint;
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

Coin make_coin(std::uint64_t value) {
  Coin coin;
  coin.output.value = value;
  return coin;
}

}  // namespace

TEST_CASE("add then find returns the coin") {
  UtxoSet set;
  const OutPoint op = make_outpoint(0x01, 0);
  CHECK(set.add(op, make_coin(100)).has_value());
  CHECK(set.contains(op));
  const Coin* found = set.find(op);
  CHECK(found != nullptr);
  CHECK_EQ(found->output.value, static_cast<std::uint64_t>(100));
  CHECK_EQ(set.size(), static_cast<std::size_t>(1));
}

TEST_CASE("duplicate add is rejected as invalid state transition") {
  UtxoSet set;
  const OutPoint op = make_outpoint(0x02, 1);
  CHECK(set.add(op, make_coin(1)).has_value());
  auto dup = set.add(op, make_coin(2));
  CHECK(!dup.has_value());
  CHECK(dup.error().code == ErrorCode::kInvalidStateTransition);
  CHECK_EQ(set.size(), static_cast<std::size_t>(1));
}

TEST_CASE("spend removes the coin and returns it") {
  UtxoSet set;
  const OutPoint op = make_outpoint(0x03, 2);
  CHECK(set.add(op, make_coin(500)).has_value());
  auto spent = set.spend(op);
  CHECK(spent.has_value());
  CHECK_EQ(spent->output.value, static_cast<std::uint64_t>(500));
  CHECK(!set.contains(op));
  CHECK(set.empty());
}

TEST_CASE("spending an unknown outpoint is rejected") {
  UtxoSet set;
  auto spent = set.spend(make_outpoint(0x04, 0));
  CHECK(!spent.has_value());
  CHECK(spent.error().code == ErrorCode::kInvalidStateTransition);
}

TEST_CASE("find returns nullptr for absent outpoint") {
  const UtxoSet set;
  CHECK(set.find(make_outpoint(0x05, 9)) == nullptr);
}

TEST_CASE("iteration order is deterministic by outpoint") {
  UtxoSet set;
  // Insert out of order; expect ascending (txid, index) iteration.
  CHECK(set.add(make_outpoint(0x10, 1), make_coin(1)).has_value());
  CHECK(set.add(make_outpoint(0x10, 0), make_coin(2)).has_value());
  CHECK(set.add(make_outpoint(0x02, 5), make_coin(3)).has_value());

  std::vector<std::pair<std::uint8_t, std::uint32_t>> seen;
  for (const auto& [outpoint, coin] : set.coins()) {
    seen.emplace_back(static_cast<std::uint8_t>(outpoint.txid[0]), outpoint.index);
  }

  const std::vector<std::pair<std::uint8_t, std::uint32_t>> expected = {
      {0x02, 5}, {0x10, 0}, {0x10, 1}};
  CHECK(seen == expected);
}
