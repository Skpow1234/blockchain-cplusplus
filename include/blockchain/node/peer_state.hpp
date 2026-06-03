#ifndef BLOCKCHAIN_NODE_PEER_STATE_HPP
#define BLOCKCHAIN_NODE_PEER_STATE_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "blockchain/consensus/chain.hpp"
#include "blockchain/consensus/params.hpp"
#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/mempool/mempool.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/net/socket_io.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/production/block_builder.hpp"
#include "blockchain/protocol/block.hpp"

namespace blockchain::node {

// In-memory chain + mempool for Stage-2 relay mode. Blocks mined locally (or the
// genesis block) are retained in a height-indexed catalog so peers can request
// them by height. All network payloads are treated as hostile until validated.
class PeerState {
 public:
  [[nodiscard]] static Result<PeerState> from_config(const NodeConfig& config);

  [[nodiscard]] net::HandshakePayload local_handshake() const;
  [[nodiscard]] std::uint32_t height() const noexcept { return chain_.height(); }
  [[nodiscard]] crypto::Hash256 tip_hash() const { return chain_.tip_hash(); }
  [[nodiscard]] std::size_t mempool_size() const noexcept { return mempool_.size(); }
  [[nodiscard]] bool mempool_contains(const crypto::Hash256& txid) const;

  // Processes one inbound P2P message. May send zero or more replies on `socket`.
  [[nodiscard]] Result<void> handle_message(const net::P2pMessage& message, net::TcpSocket& socket);

  [[nodiscard]] Result<void> accept_transaction(const protocol::Transaction& tx);

  // Mines `count` blocks on top of the current tip using the mempool and local config.
  [[nodiscard]] Result<void> mine_blocks(std::uint32_t count);

  // Writes the catalog to data_dir/ledger.bin when persist is enabled in config.
  [[nodiscard]] Result<void> persist_ledger() const;

  [[nodiscard]] const protocol::Block* block_at_height(std::uint32_t height) const noexcept;

 private:
  PeerState(consensus::Chain chain, mempool::Mempool mempool, std::string node_id,
            std::uint64_t genesis_timestamp, consensus::ConsensusParams consensus,
            production::BlockTemplateParams tmpl_params, std::string data_dir, bool persist,
            std::uint32_t mine_after_tx);

  [[nodiscard]] Result<void> connect_block(protocol::Block block);
  [[nodiscard]] std::vector<protocol::Block> ledger_blocks() const;

  [[nodiscard]] Result<void> on_tx_announce(std::span<const std::byte> payload_bytes);
  [[nodiscard]] Result<void> on_block_announce(std::span<const std::byte> payload_bytes);
  [[nodiscard]] Result<void> on_block_request(const net::BlockRequestPayload& request,
                                              net::TcpSocket& socket) const;
  [[nodiscard]] Result<void> on_block_response(std::span<const std::byte> payload_bytes);

  void store_block(std::uint32_t height, protocol::Block block);

  consensus::Chain chain_;
  mempool::Mempool mempool_;
  std::string node_id_;
  std::uint64_t genesis_timestamp_ = 0;
  consensus::ConsensusParams consensus_{};
  production::BlockTemplateParams tmpl_params_{};
  std::string data_dir_;
  bool persist_ = false;
  std::uint32_t mine_after_tx_ = 0;
  std::map<std::uint32_t, protocol::Block> catalog_;
};

}  // namespace blockchain::node

#endif  // BLOCKCHAIN_NODE_PEER_STATE_HPP
