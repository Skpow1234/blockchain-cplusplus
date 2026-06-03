#include "blockchain/crypto/hash.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/node/peer_state.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::crypto::Hash256;
using blockchain::crypto::to_hex;
using blockchain::node::NodeConfig;
using blockchain::node::PeerState;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;

namespace {

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

Transaction make_spend(const OutPoint& prevout, std::uint64_t input_value, std::uint64_t fee,
                       const Hash256& to) {
  Transaction tx;
  TxInput input;
  input.prevout = prevout;
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = input_value - fee;
  output.recipient = to;
  tx.outputs.push_back(output);
  return tx;
}

}  // namespace

TEST_CASE("peer state accepts a spend after a mined coinbase") {
  NodeConfig config;
  config.genesis_timestamp = 2'000;
  config.block_subsidy = 5'000;
  config.coinbase_maturity = 1;
  config.coinbase_recipient_hex = to_hex(tag_hash(0xA1));
  config.mine_blocks = 1;

  auto state = PeerState::from_config(config);
  CHECK(state.has_value());

  const auto* block1 = state->block_at_height(1);
  CHECK(block1 != nullptr);
  CHECK(!block1->transactions.empty());

  OutPoint coinbase_out;
  coinbase_out.txid = block1->transactions.front().txid();
  coinbase_out.index = 0;

  const Transaction spend = make_spend(coinbase_out, 5'000, 100, tag_hash(0xB2));
  CHECK(state->accept_transaction(spend).has_value());
  CHECK(state->mempool_contains(spend.txid()));
  CHECK_EQ(state->mempool_size(), static_cast<std::size_t>(1));
}
