// End-to-end integration test: drives the full Stage-1 loop across several
// blocks using the real components (Chain + block production + mempool +
// validation), with no networking or storage. It demonstrates coins entering
// via a coinbase, maturing, being spent through the mempool, and the mempool
// being pruned as blocks connect -- all deterministically.
#include <cstdint>
#include <vector>

#include "blockchain/consensus/chain.hpp"
#include "blockchain/consensus/params.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/block.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "testing.hpp"

using blockchain::consensus::Chain;
using blockchain::consensus::ConsensusParams;
using blockchain::crypto::Hash256;
using blockchain::crypto::zero_hash;
using blockchain::mempool::Mempool;
using blockchain::mempool::MempoolLimits;
using blockchain::production::BlockTemplateParams;
using blockchain::production::build_block_template;
using blockchain::protocol::Block;
using blockchain::protocol::compute_merkle_root;
using blockchain::protocol::kBlockVersion;
using blockchain::protocol::kMaxBlockSizeBytes;
using blockchain::protocol::OutPoint;
using blockchain::protocol::Transaction;
using blockchain::protocol::TxInput;
using blockchain::protocol::TxOutput;

namespace {

constexpr std::uint64_t kSubsidy = 5'000;
constexpr std::uint64_t kGenesisTime = 1'000;

Hash256 tag_hash(std::uint8_t tag) {
  Hash256 hash{};
  hash.fill(std::byte{tag});
  return hash;
}

Block make_genesis() {
  Block genesis;
  genesis.header.version = kBlockVersion;
  genesis.header.prev_block = zero_hash();
  genesis.header.height = 0;
  genesis.header.timestamp = kGenesisTime;
  genesis.header.merkle_root = compute_merkle_root(genesis.transactions);
  return genesis;
}

BlockTemplateParams template_params(const ConsensusParams& consensus, const Hash256& recipient) {
  BlockTemplateParams params;
  params.version = kBlockVersion;
  params.max_block_size_bytes = kMaxBlockSizeBytes;
  params.max_transactions = 1'000;
  params.consensus = consensus;
  params.coinbase_recipient = recipient;
  return params;
}

// Spends `prevout` entirely except for `fee`, sending the remainder to `to`.
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

TEST_CASE("end-to-end: mine, mature, spend, and prune across blocks") {
  // Maturity of 1 lets a coinbase minted at height 1 be spent at height 2.
  const ConsensusParams consensus{.block_subsidy = kSubsidy, .coinbase_maturity = 1};
  const Hash256 miner = tag_hash(0xA1);
  const Hash256 payee = tag_hash(0xB2);

  auto chain = Chain::create(make_genesis(), consensus);
  CHECK(chain.has_value());

  // Block 1: empty mempool -> a coinbase paying the subsidy to the miner.
  Mempool pool(MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U});
  auto tmpl1 = build_block_template(chain->tip(), kGenesisTime + 1, pool, chain->utxos(),
                                    template_params(consensus, miner));
  CHECK(tmpl1.has_value());
  CHECK_EQ(tmpl1->selected_count, static_cast<std::size_t>(0));
  CHECK(chain->submit_block(tmpl1->block).has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(1));

  // The coinbase output from block 1 is now a (coinbase) coin in chain state.
  OutPoint coinbase_out;
  coinbase_out.txid = tmpl1->block.transactions.front().txid();
  coinbase_out.index = 0;
  CHECK(chain->utxos().contains(coinbase_out));

  // Submit a transaction spending that coinbase (fee 100) into the mempool.
  const Transaction spend = make_spend(coinbase_out, kSubsidy, /*fee=*/100, payee);
  CHECK(pool.accept(spend, chain->utxos()).has_value());
  CHECK_EQ(pool.size(), static_cast<std::size_t>(1));

  // Block 2: at height 2 the coinbase is mature, so the spend is selected. The
  // new coinbase collects subsidy + the 100 fee.
  auto tmpl2 = build_block_template(chain->tip(), kGenesisTime + 2, pool, chain->utxos(),
                                    template_params(consensus, miner));
  CHECK(tmpl2.has_value());
  CHECK_EQ(tmpl2->selected_count, static_cast<std::size_t>(1));
  CHECK_EQ(tmpl2->total_fees, static_cast<std::uint64_t>(100));
  CHECK(chain->submit_block(tmpl2->block, &pool).has_value());
  CHECK_EQ(chain->height(), static_cast<std::uint32_t>(2));

  // Spending the coinbase consumed it; the payee now holds a coin; the mempool
  // was pruned of the included transaction.
  CHECK(!chain->utxos().contains(coinbase_out));
  OutPoint payee_out;
  payee_out.txid = spend.txid();
  payee_out.index = 0;
  const auto* payee_coin = chain->utxos().find(payee_out);
  CHECK(payee_coin != nullptr);
  CHECK_EQ(payee_coin->output.value, kSubsidy - 100);
  CHECK(pool.empty());
}

TEST_CASE("end-to-end: spending an immature coinbase is not selectable") {
  // With a high maturity, the block-1 coinbase cannot be spent at height 2.
  const ConsensusParams consensus{.block_subsidy = kSubsidy, .coinbase_maturity = 100};
  const Hash256 miner = tag_hash(0xA1);
  const Hash256 payee = tag_hash(0xB2);

  auto chain = Chain::create(make_genesis(), consensus);
  CHECK(chain.has_value());

  Mempool pool(MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U});
  auto tmpl1 = build_block_template(chain->tip(), kGenesisTime + 1, pool, chain->utxos(),
                                    template_params(consensus, miner));
  CHECK(tmpl1.has_value());
  CHECK(chain->submit_block(tmpl1->block).has_value());

  OutPoint coinbase_out;
  coinbase_out.txid = tmpl1->block.transactions.front().txid();
  coinbase_out.index = 0;
  CHECK(pool.accept(make_spend(coinbase_out, kSubsidy, 100, payee), chain->utxos()).has_value());

  // The producer must not include the immature spend; block 2 carries only the
  // coinbase and collects no fees.
  auto tmpl2 = build_block_template(chain->tip(), kGenesisTime + 2, pool, chain->utxos(),
                                    template_params(consensus, miner));
  CHECK(tmpl2.has_value());
  CHECK_EQ(tmpl2->selected_count, static_cast<std::size_t>(0));
  CHECK_EQ(tmpl2->total_fees, static_cast<std::uint64_t>(0));
}

TEST_CASE("end-to-end: the same inputs produce an identical chain tip") {
  const ConsensusParams consensus{.block_subsidy = kSubsidy, .coinbase_maturity = 1};
  const Hash256 miner = tag_hash(0xA1);
  const Hash256 payee = tag_hash(0xB2);

  auto run = [&]() -> Hash256 {
    auto chain = Chain::create(make_genesis(), consensus);
    Mempool pool(MempoolLimits{.max_transactions = 100, .max_total_bytes = 1U << 20U});

    auto b1 = build_block_template(chain->tip(), kGenesisTime + 1, pool, chain->utxos(),
                                   template_params(consensus, miner));
    (void)chain->submit_block(b1->block);

    OutPoint coinbase_out;
    coinbase_out.txid = b1->block.transactions.front().txid();
    coinbase_out.index = 0;
    (void)pool.accept(make_spend(coinbase_out, kSubsidy, 100, payee), chain->utxos());

    auto b2 = build_block_template(chain->tip(), kGenesisTime + 2, pool, chain->utxos(),
                                   template_params(consensus, miner));
    (void)chain->submit_block(b2->block, &pool);
    return chain->tip_hash();
  };

  CHECK(run() == run());
}
