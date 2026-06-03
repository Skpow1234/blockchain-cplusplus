#include "blockchain/node/peer_state.hpp"

#include "blockchain/crypto/hash.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "blockchain/storage/chain_store.hpp"
#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::node {
namespace {

[[nodiscard]] consensus::ConsensusParams build_consensus_params(const NodeConfig& config) {
  consensus::ConsensusParams params;
  if (config.block_subsidy != 0) {
    params.block_subsidy = config.block_subsidy;
  }
  if (config.coinbase_maturity != 0) {
    params.coinbase_maturity = config.coinbase_maturity;
  }
  return params;
}

[[nodiscard]] Result<crypto::Hash256> resolve_coinbase_recipient(const NodeConfig& config) {
  if (config.coinbase_recipient_hex.empty()) {
    return crypto::zero_hash();
  }
  return crypto::hash_from_hex(config.coinbase_recipient_hex);
}

[[nodiscard]] production::BlockTemplateParams build_template_params(
    const NodeConfig& config, const consensus::ConsensusParams& consensus,
    const crypto::Hash256& recipient) {
  production::BlockTemplateParams params;
  params.version = protocol::kBlockVersion;
  params.max_block_size_bytes =
      config.max_block_size_bytes != 0 ? config.max_block_size_bytes : protocol::kMaxBlockSizeBytes;
  params.max_transactions = protocol::kMaxTransactionsPerBlock;
  params.consensus = consensus;
  params.coinbase_recipient = recipient;
  return params;
}

[[nodiscard]] std::uint64_t block_timestamp(std::uint64_t genesis_timestamp, std::uint32_t height) {
  return genesis_timestamp + static_cast<std::uint64_t>(height);
}

[[nodiscard]] Result<void> send_reject(net::TcpSocket& socket, net::RejectCode code,
                                       std::string_view reason) {
  auto msg =
      net::make_reject_message(net::RejectPayload{.code = code, .reason = std::string(reason)});
  if (!msg) {
    return std::unexpected(msg.error());
  }
  return net::send_message(socket, *msg);
}

[[nodiscard]] Result<protocol::Block> parse_block_bytes(std::span<const std::byte> bytes) {
  serialization::ByteReader reader(bytes);
  auto block = protocol::Block::deserialize(reader);
  if (!block) {
    return std::unexpected(block.error());
  }
  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return *block;
}

[[nodiscard]] Result<protocol::Transaction> parse_transaction_bytes(
    std::span<const std::byte> bytes) {
  serialization::ByteReader reader(bytes);
  auto tx = protocol::Transaction::deserialize(reader);
  if (!tx) {
    return std::unexpected(tx.error());
  }
  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return *tx;
}

}  // namespace

PeerState::PeerState(consensus::Chain chain, mempool::Mempool mempool,
                     mempool::MempoolPolicy mempool_policy, std::string node_id,
                     std::uint64_t genesis_timestamp, consensus::ConsensusParams consensus,
                     production::BlockTemplateParams tmpl_params, std::string data_dir,
                     bool persist, std::uint32_t mine_after_tx)
    : chain_(std::move(chain)),
      mempool_(std::move(mempool)),
      mempool_policy_(mempool_policy),
      node_id_(std::move(node_id)),
      genesis_timestamp_(genesis_timestamp),
      consensus_(consensus),
      tmpl_params_(tmpl_params),
      data_dir_(std::move(data_dir)),
      persist_(persist),
      mine_after_tx_(mine_after_tx) {}

Result<PeerState> PeerState::from_config(const NodeConfig& config) {
  const consensus::ConsensusParams consensus = build_consensus_params(config);
  auto recipient = resolve_coinbase_recipient(config);
  if (!recipient) {
    return std::unexpected(recipient.error());
  }
  const production::BlockTemplateParams tmpl_params =
      build_template_params(config, consensus, *recipient);
  const mempool::MempoolLimits pool_limits = resolved_mempool_limits(config);
  const mempool::MempoolPolicy pool_policy = resolved_mempool_policy(config);

  if (config.restore) {
    storage::ChainStore store(config.data_dir);
    if (!store.ledger_exists()) {
      return make_error(ErrorCode::kInvalidConfig, "no ledger found in data_dir for --restore");
    }
    auto chain = store.load_chain();
    if (!chain) {
      return std::unexpected(chain.error());
    }
    auto ledger = store.load_ledger();
    if (!ledger) {
      return std::unexpected(ledger.error());
    }

    mempool::Mempool pool(pool_limits);
    PeerState state(std::move(*chain), std::move(pool), pool_policy, config.node_id,
                    config.genesis_timestamp, consensus, tmpl_params, config.data_dir,
                    config.persist, config.mine_after_tx);
    for (const protocol::Block& block : ledger->first) {
      state.store_block(block.header.height, block);
    }
    return state;
  }

  const protocol::Block genesis = build_genesis_block(config);
  auto chain = consensus::Chain::create(genesis, consensus);
  if (!chain) {
    return std::unexpected(chain.error());
  }

  mempool::Mempool pool(pool_limits);

  PeerState state(std::move(*chain), std::move(pool), pool_policy, config.node_id,
                  config.genesis_timestamp, consensus, tmpl_params, config.data_dir,
                  config.persist, config.mine_after_tx);
  state.store_block(0, genesis);

  if (auto mined = state.mine_blocks(config.mine_blocks); !mined) {
    return std::unexpected(mined.error());
  }

  if (config.persist) {
    if (auto saved = state.persist_ledger(); !saved) {
      return std::unexpected(saved.error());
    }
  }

  return state;
}

net::HandshakePayload PeerState::local_handshake() const {
  net::HandshakePayload hs;
  hs.network_magic = net::kNetworkMagic;
  hs.height = chain_.height();
  hs.tip_hash = chain_.tip_hash();
  hs.node_id = node_id_;
  return hs;
}

void PeerState::store_block(std::uint32_t height, protocol::Block block) {
  catalog_[height] = std::move(block);
}

bool PeerState::mempool_contains(const crypto::Hash256& txid) const {
  return mempool_.contains(txid);
}

const protocol::Block* PeerState::block_at_height(std::uint32_t height) const noexcept {
  const auto it = catalog_.find(height);
  if (it == catalog_.end()) {
    return nullptr;
  }
  return &it->second;
}

std::vector<protocol::Block> PeerState::ledger_blocks() const {
  std::vector<protocol::Block> blocks;
  blocks.reserve(catalog_.size());
  for (const auto& [height, block] : catalog_) {
    (void)height;
    blocks.push_back(block);
  }
  return blocks;
}

Result<void> PeerState::persist_ledger() const {
  if (!persist_) {
    return {};
  }
  return save_ledger();
}

Result<void> PeerState::save_ledger() const {
  storage::ChainStore store(data_dir_);
  return store.save_ledger(ledger_blocks(), consensus_);
}

Result<void> PeerState::connect_block(protocol::Block block) {
  auto fees = chain_.submit_block(block, &mempool_);
  if (!fees) {
    return std::unexpected(fees.error());
  }
  store_block(chain_.height(), std::move(block));
  return {};
}

Result<void> PeerState::mine_blocks(std::uint32_t count) {
  for (std::uint32_t n = 0; n < count; ++n) {
    const std::uint64_t timestamp = block_timestamp(genesis_timestamp_, chain_.height() + 1);
    auto tmpl = production::build_block_template(chain_.tip(), timestamp, mempool_, chain_.utxos(),
                                                 tmpl_params_);
    if (!tmpl) {
      return std::unexpected(tmpl.error());
    }
    if (auto ok = connect_block(tmpl->block); !ok) {
      return ok;
    }
  }
  return persist_ledger();
}

Result<void> PeerState::accept_transaction(const protocol::Transaction& tx) {
  return mempool_.accept(tx, chain_.utxos(), mempool_policy_);
}

Result<void> PeerState::on_tx_announce(std::span<const std::byte> payload_bytes) {
  auto parsed = net::deserialize_tx_announce(payload_bytes);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  auto tx = parse_transaction_bytes(
      std::span<const std::byte>(parsed->tx_bytes.data(), parsed->tx_bytes.size()));
  if (!tx) {
    return std::unexpected(tx.error());
  }
  if (auto ok = accept_transaction(*tx); !ok) {
    return ok;
  }
  if (mine_after_tx_ > 0) {
    return mine_blocks(mine_after_tx_);
  }
  return {};
}

Result<void> PeerState::on_block_announce(std::span<const std::byte> payload_bytes) {
  auto parsed = net::deserialize_block_announce(payload_bytes);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  auto block = parse_block_bytes(
      std::span<const std::byte>(parsed->block_bytes.data(), parsed->block_bytes.size()));
  if (!block) {
    return std::unexpected(block.error());
  }
  if (auto ok = connect_block(std::move(*block)); !ok) {
    return ok;
  }
  return persist_ledger();
}

Result<void> PeerState::on_block_request(const net::BlockRequestPayload& request,
                                         net::TcpSocket& socket) const {
  const protocol::Block* block = block_at_height(request.height);
  if (block == nullptr) {
    return send_reject(socket, net::RejectCode::kInvalidMessage, "unknown block height");
  }
  net::BlockResponsePayload response;
  response.block_bytes = block->to_bytes();
  auto msg = net::make_block_response_message(response);
  if (!msg) {
    return std::unexpected(msg.error());
  }
  return net::send_message(socket, *msg);
}

Result<void> PeerState::on_block_response(std::span<const std::byte> payload_bytes) {
  return on_block_announce(payload_bytes);
}

Result<void> PeerState::handle_message(const net::P2pMessage& message, net::TcpSocket& socket) {
  switch (message.type) {
    case net::P2pMessageType::kTxAnnounce: {
      auto parsed = net::parse_tx_announce_message(message);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      if (auto ok = on_tx_announce(
              std::span<const std::byte>(message.payload.data(), message.payload.size()));
          !ok) {
        if (auto reject = send_reject(socket, net::RejectCode::kInvalidMessage, ok.error().message);
            !reject) {
          return reject;
        }
        return ok;
      }
      return {};
    }
    case net::P2pMessageType::kBlockAnnounce: {
      auto parsed = net::parse_block_announce_message(message);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      return on_block_announce(
          std::span<const std::byte>(message.payload.data(), message.payload.size()));
    }
    case net::P2pMessageType::kBlockRequest: {
      auto parsed = net::parse_block_request_message(message);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      return on_block_request(*parsed, socket);
    }
    case net::P2pMessageType::kBlockResponse: {
      auto parsed = net::parse_block_response_message(message);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      return on_block_response(
          std::span<const std::byte>(message.payload.data(), message.payload.size()));
    }
    case net::P2pMessageType::kPing: {
      auto ping = net::parse_ping_message(message);
      if (!ping) {
        return std::unexpected(ping.error());
      }
      auto pong = net::make_pong_message(net::PongPayload{.nonce = ping->nonce});
      if (!pong) {
        return std::unexpected(pong.error());
      }
      return net::send_message(socket, *pong);
    }
    case net::P2pMessageType::kPong:
      return {};
    default:
      if (auto reject =
              send_reject(socket, net::RejectCode::kInvalidMessage, "unsupported message type");
          !reject) {
        return reject;
      }
      return make_error(ErrorCode::kInvalidMessage, "unsupported message type");
  }
}

}  // namespace blockchain::node
