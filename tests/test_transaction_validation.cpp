#include <cstdint>

#include "blockchain/error.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "blockchain/validation/transaction_validation.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;
using blockchain::validation::check_transaction_inputs;
using blockchain::validation::check_transaction_sanity;

namespace {

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

TxInput make_input(std::uint8_t tag, std::uint32_t index) {
  TxInput input;
  input.prevout = make_outpoint(tag, index);
  return input;
}

TxOutput make_output(std::uint64_t value) {
  TxOutput output;
  output.value = value;
  return output;
}

// A funded UTXO set where outpoint (tag,index) holds `value`.
UtxoSet funded(std::uint8_t tag, std::uint32_t index, std::uint64_t value) {
  UtxoSet set;
  Coin coin;
  coin.output.value = value;
  (void)set.add(make_outpoint(tag, index), coin);
  return set;
}

}  // namespace

TEST_CASE("sanity: well-formed transaction passes") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x01, 0));
  tx.outputs.push_back(make_output(10));
  CHECK(check_transaction_sanity(tx).has_value());
}

TEST_CASE("sanity: empty inputs rejected") {
  Transaction tx;
  tx.outputs.push_back(make_output(10));
  auto result = check_transaction_sanity(tx);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("sanity: empty outputs rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x01, 0));
  CHECK(!check_transaction_sanity(tx).has_value());
}

TEST_CASE("sanity: duplicate inputs rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x01, 0));
  tx.inputs.push_back(make_input(0x01, 0));
  tx.outputs.push_back(make_output(1));
  auto result = check_transaction_sanity(tx);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("sanity: output above max money rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x01, 0));
  tx.outputs.push_back(make_output(blockchain::protocol::kMaxMoney + 1));
  CHECK(!check_transaction_sanity(tx).has_value());
}

TEST_CASE("sanity: output sum overflow rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x01, 0));
  tx.outputs.push_back(make_output(UINT64_MAX));
  tx.outputs.push_back(make_output(2));
  CHECK(!check_transaction_sanity(tx).has_value());
}

TEST_CASE("inputs: fee is sum(inputs) - sum(outputs)") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x02, 0));
  tx.outputs.push_back(make_output(70));
  const UtxoSet set = funded(0x02, 0, 100);
  auto fee = check_transaction_inputs(tx, set);
  CHECK(fee.has_value());
  CHECK_EQ(fee.value(), static_cast<std::uint64_t>(30));
}

TEST_CASE("inputs: missing input rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x03, 0));
  tx.outputs.push_back(make_output(1));
  const UtxoSet empty;
  auto result = check_transaction_inputs(tx, empty);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("inputs: value creation rejected") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x04, 0));
  tx.outputs.push_back(make_output(150));
  const UtxoSet set = funded(0x04, 0, 100);
  auto result = check_transaction_inputs(tx, set);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kInvalidTransaction);
}

TEST_CASE("inputs: exact-spend yields zero fee") {
  Transaction tx;
  tx.inputs.push_back(make_input(0x05, 1));
  tx.outputs.push_back(make_output(100));
  const UtxoSet set = funded(0x05, 1, 100);
  auto fee = check_transaction_inputs(tx, set);
  CHECK(fee.has_value());
  CHECK_EQ(fee.value(), static_cast<std::uint64_t>(0));
}
