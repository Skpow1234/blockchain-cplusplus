#include <cstdint>
#include <utility>
#include <vector>

#include "blockchain/consensus/chain.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/state/utxo_set.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::consensus::Chain;
using blockchain::crypto::zero_hash;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::protocol::Block;
using blockchain::protocol::BlockHeader;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::kBlockVersion;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;
using blockchain::state::Coin;
using blockchain::state::UtxoSet;

namespace {

constexpr std::uint64_t kGenesisTime = 1'000;

Block make_genesis(std::uint64_t timestamp = kGenesisTime) {
  Block genesis;
  genesis.header.version = kBlockVersion;
  genesis.header.prev_block = zero_hash();
  genesis.header.height = 0;
  genesis.header.timestamp = timestamp;
  genesis.header.merkle_root = compute_merkle_root(genesis.transactions);
  return genesis;
}

// Builds a block that correctly links onto `tip`: right version, prev hash,
// height, and a Merkle root over `txs`.
Block make_child(const BlockHeader& tip, std::vector<Transaction> txs, std::uint64_t timestamp) {
  Block block;
  block.header.version = kBlockVersion;
  block.header.prev_block = tip.hash();
  block.header.height = tip.height + 1;
  block.header.timestamp = timestamp;
  block.transactions = std::move(txs);
  block.header.merkle_root = compute_merkle_root(block.transactions);
  return block;
}

OutPoint make_outpoint(std::uint8_t tag, std::uint32_t index) {
  OutPoint out;
  out.txid.fill(std::byte{tag});
  out.index = index;
  return out;
}

Transaction spend_tx(std::uint8_t tag, std::uint32_t index, std::uint64_t value) {
  Transaction tx;
  TxInput input;
  input.prevout = make_outpoint(tag, index);
  tx.inputs.push_back(input);
  TxOutput output;
  output.value = value;
  tx.outputs.push_back(output);
  return tx;
}

}  // namespace

TEST_CASE("create accepts a well-formed genesis") {
  auto chain = Chain::create(make_genesis());
  CHECK(chain.has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(0));
  CHECK(chain->utxos().empty());
  CHECK(chain->tip_hash() == make_genesis().header.hash());
}

TEST_CASE("create rejects a genesis with nonzero height") {
  Block genesis = make_genesis();
  genesis.header.height = 1;
  auto chain = Chain::create(genesis);
  CHECK(!chain.has_value());
  CHECK(chain.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("create rejects a genesis that references a nonzero prev hash") {
  Block genesis = make_genesis();
  genesis.header.prev_block.fill(std::byte{0xAB});
  auto chain = Chain::create(genesis);
  CHECK(!chain.has_value());
  CHECK(chain.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("create rejects an unsupported genesis version") {
  Block genesis = make_genesis();
  genesis.header.version = kBlockVersion + 1;
  auto chain = Chain::create(genesis);
  CHECK(!chain.has_value());
  CHECK(chain.error().code == ErrorCode::kUnsupportedVersion);
}

TEST_CASE("submit advances the tip with an empty block") {
  auto chain = Chain::create(make_genesis());
  const Block block = make_child(chain->tip(), {}, kGenesisTime + 1);

  auto fees = chain->submit_block(block);
  CHECK(fees.has_value());
  CHECK_EQ(fees.value(), static_cast<std::uint64_t>(0));
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(1));
  CHECK(chain->tip_hash() == block.header.hash());
}

TEST_CASE("submit rejects a block that does not extend the tip") {
  auto chain = Chain::create(make_genesis());
  Block block = make_child(chain->tip(), {}, kGenesisTime + 1);
  block.header.prev_block.fill(std::byte{0x11});  // breaks the link

  auto fees = chain->submit_block(block);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidBlock);
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(0));  // tip unchanged
}

TEST_CASE("submit rejects a block with the wrong height") {
  auto chain = Chain::create(make_genesis());
  Block block = make_child(chain->tip(), {}, kGenesisTime + 1);
  block.header.height = 5;  // should be 1

  auto fees = chain->submit_block(block);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("submit rejects a timestamp earlier than the tip") {
  auto chain = Chain::create(make_genesis());
  const Block block = make_child(chain->tip(), {}, kGenesisTime - 1);

  auto fees = chain->submit_block(block);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidBlock);
}

TEST_CASE("submit allows an equal (non-decreasing) timestamp") {
  auto chain = Chain::create(make_genesis());
  const Block block = make_child(chain->tip(), {}, kGenesisTime);
  CHECK(chain->submit_block(block).has_value());
}

TEST_CASE("submit rejects an unsupported block version") {
  auto chain = Chain::create(make_genesis());
  Block block = make_child(chain->tip(), {}, kGenesisTime + 1);
  block.header.version = kBlockVersion + 1;

  auto fees = chain->submit_block(block);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kUnsupportedVersion);
}

TEST_CASE("submit is all-or-nothing when a contained transaction is invalid") {
  auto chain = Chain::create(make_genesis());
  // Spends an outpoint that does not exist in the (empty) chain UTXO set.
  const Block block = make_child(chain->tip(), {spend_tx(0x42, 0, 10)}, kGenesisTime + 1);

  auto fees = chain->submit_block(block);
  CHECK(!fees.has_value());
  CHECK(fees.error().code == ErrorCode::kInvalidTransaction);
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(0));
  CHECK(chain->utxos().empty());
}

TEST_CASE("a sequence of empty blocks is deterministic") {
  auto build_three = []() {
    auto chain = Chain::create(make_genesis());
    for (std::uint32_t i = 1; i <= 3; ++i) {
      const Block block = make_child(chain->tip(), {}, kGenesisTime + i);
      (void)chain->submit_block(block);
    }
    return chain->tip_hash();
  };
  CHECK(build_three() == build_three());
}

TEST_CASE("submitting a block leaves unrelated mempool entries intact") {
  auto chain = Chain::create(make_genesis());

  // Fund a standalone UTXO set so the mempool has something valid to admit.
  UtxoSet funded;
  Coin coin;
  coin.output.value = 100;
  (void)funded.add(make_outpoint(0x55, 0), coin);

  Mempool pool(MempoolLimits{.max_transactions = 16, .max_total_bytes = 1U << 20U});
  CHECK(pool.accept(spend_tx(0x55, 0, 70), funded).has_value());
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));

  // The empty block includes none of the pooled transactions, so pruning is a
  // no-op and the entry survives.
  const Block block = make_child(chain->tip(), {}, kGenesisTime + 1);
  CHECK(chain->submit_block(block, &pool).has_value());
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));
}
