#include "blockchain/node/peer_state.hpp"

#include "blockchain/crypto/hash.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "blockchain/validation/transaction_validation.hpp"

namespace blockchain::node {
namespace {

constexpr std::uint32_t kRelayMempoolMaxTransactions = 10'000;
constexpr std::uint64_t kRelayMempoolMaxBytes =
    100ULL * static_cast<std::uint64_t>(protocol::kMaxBlockSizeBytes);

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
  params.max_block_size_bytes = config.max_block_size_bytes != 0 ? config.max_block_size_bytes
                                                                 : protocol::kMaxBlockSizeBytes;
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
  auto msg = net::make_reject_message(net::RejectPayload{.code = code, .reason = std::string(reason)});
  if (!msg) {
    return std::unexpected(msg.error());
  }
  return net::send_message(socket, *msg);
}

}  // namespace

PeerState::PeerState(consensus::Chain chain, mempool::Mempool mempool, std::string node_id)
    : chain_(std::move(chain)), mempool_(std::move(mempool)), node_id_(std::move(node_id)) {}

Result<PeerState> PeerState::from_config(const NodeConfig& config) {
  const consensus::ConsensusParams consensus = build_consensus_params(config);
  auto recipient = resolve_coinbase_recipient(config);
  if (!recipient) {
    return std::unexpected(recipient.error());
  }

  const protocol::Block genesis = build_genesis_block(config);
  auto chain = consensus::Chain::create(genesis, consensus);
  if (!chain) {
    return std::unexpected(chain.error());
  }

  mempool::Mempool pool(mempool::MempoolLimits{.max_transactions = kRelayMempoolMaxTransactions,
                                               .max_total_bytes = kRelayMempoolMaxBytes});
  const production::BlockTemplateParams tmpl_params =
      build_template_params(config, consensus, *recipient);

  PeerState state(std::move(*chain), std::move(pool), config.node_id);
  state.store_block(0, genesis);

  for (std::uint32_t n = 0; n < config.mine_blocks; ++n) {
    const std::uint64_t timestamp =
        block_timestamp(config.genesis_timestamp, state.chain_.height() + 1);
    auto tmpl = production::build_block_template(state.chain_.tip(), timestamp, state.mempool_,
                                                 state.chain_.utxos(), tmpl_params);
    if (!tmpl) {
      return std::unexpected(tmpl.error());
    }
    auto fees = state.chain_.submit_block(tmpl->block, &state.mempool_);
    if (!fees) {
      return std::unexpected(fees.error());
    }
    state.store_block(state.chain_.height(), tmpl->block);
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

Result<protocol::Block> PeerState::parse_block_bytes(std::span<const std::byte> bytes) const {
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

Result<protocol::Transaction> PeerState::parse_transaction_bytes(
    std::span<const std::byte> bytes) const {
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

Result<void> PeerState::accept_transaction(const protocol::Transaction& tx) {
  if (auto sanity = validation::check_transaction_sanity(tx); !sanity) {
    return std::unexpected(sanity.error());
  }
  return mempool_.accept(tx, chain_.utxos());
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
  return accept_transaction(*tx);
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
  auto fees = chain_.submit_block(*block, &mempool_);
  if (!fees) {
    return std::unexpected(fees.error());
  }
  store_block(chain_.height(), *block);
  return {};
}

Result<void> PeerState::on_block_request(const net::BlockRequestPayload& request,
                                         net::TcpSocket& socket) {
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
  auto parsed = net::deserialize_block_response(payload_bytes);
  if (!parsed) {
    return std::unexpected(parsed.error());
  }
  return on_block_announce(payload_bytes);
}

Result<void> PeerState::handle_message(const net::P2pMessage& message, net::TcpSocket& socket) {
  switch (message.type) {
    case net::P2pMessageType::kTxAnnounce: {
      auto parsed = net::parse_tx_announce_message(message);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      if (auto ok = on_tx_announce(std::span<const std::byte>(message.payload.data(),
                                                              message.payload.size()));
          !ok) {
        (void)send_reject(socket, net::RejectCode::kInvalidMessage, ok.error().message);
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
      (void)send_reject(socket, net::RejectCode::kInvalidMessage, "unsupported message type");
      return make_error(ErrorCode::kInvalidMessage, "unsupported message type");
  }
}

}  // namespace blockchain::node
