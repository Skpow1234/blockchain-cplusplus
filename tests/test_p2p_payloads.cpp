#include <cstdint>
#include <string>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/net/p2p_payloads.hpp"
#include "blockchain/node/genesis.hpp"
#include "blockchain/node/config.hpp"
#include "blockchain/protocol/constants.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::crypto::zero_hash;
using blockchain::net::HandshakePayload;
using blockchain::net::kMaxNodeIdBytes;
using blockchain::net::kMaxRejectReasonBytes;
using blockchain::net::kNetworkMagic;
using blockchain::net::make_handshake_message;
using blockchain::net::make_ping_message;
using blockchain::net::make_pong_message;
using blockchain::net::make_reject_message;
using blockchain::net::P2pMessage;
using blockchain::net::P2pMessageType;
using blockchain::net::parse_handshake_message;
using blockchain::net::parse_ping_message;
using blockchain::net::parse_pong_message;
using blockchain::net::parse_reject_message;
using blockchain::net::PingPayload;
using blockchain::net::RejectCode;
using blockchain::net::RejectPayload;
using blockchain::net::BlockAnnouncePayload;
using blockchain::net::BlockRequestPayload;
using blockchain::net::TxAnnouncePayload;
using blockchain::net::make_block_announce_message;
using blockchain::net::make_block_request_message;
using blockchain::net::make_tx_announce_message;
using blockchain::net::parse_block_announce_message;
using blockchain::net::parse_block_request_message;
using blockchain::net::parse_tx_announce_message;
using blockchain::net::serialize_handshake;
using blockchain::node::NodeConfig;
using blockchain::node::build_genesis_block;
using blockchain::protocol::kProtocolVersion;

namespace {

HandshakePayload sample_handshake() {
  HandshakePayload hs;
  hs.network_magic = kNetworkMagic;
  hs.height = 42;
  hs.tip_hash = zero_hash();
  hs.node_id = "node-alpha";
  return hs;
}

}  // namespace

TEST_CASE("handshake payload round-trips through the full wire envelope") {
  auto encoded = make_handshake_message(sample_handshake());
  CHECK(encoded.has_value());

  const std::vector<std::byte> wire = encoded->to_bytes();
  auto envelope = P2pMessage::deserialize(wire);
  CHECK(envelope.has_value());
  CHECK(envelope->type == P2pMessageType::kHandshake);

  auto decoded = parse_handshake_message(*envelope);
  CHECK(decoded.has_value());
  CHECK_EQ(decoded->height, static_cast<std::uint32_t>(42));
  CHECK(decoded->node_id == "node-alpha");
  CHECK(decoded->tip_hash == zero_hash());
}

TEST_CASE("handshake rejects wrong network magic in payload") {
  HandshakePayload hs = sample_handshake();
  hs.network_magic = kNetworkMagic + 1;
  auto bytes = serialize_handshake(hs);
  CHECK(bytes.has_value());

  auto decoded = blockchain::net::deserialize_handshake(
      std::span<const std::byte>(bytes->data(), bytes->size()));
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kInvalidMessage);
}

TEST_CASE("handshake rejects empty node_id") {
  HandshakePayload hs = sample_handshake();
  hs.node_id.clear();
  CHECK(!serialize_handshake(hs).has_value());
}

TEST_CASE("handshake rejects oversized node_id") {
  HandshakePayload hs = sample_handshake();
  hs.node_id = std::string(static_cast<std::size_t>(kMaxNodeIdBytes) + 1, 'x');
  CHECK(!serialize_handshake(hs).has_value());
}

TEST_CASE("reject payload round-trips on the wire") {
  RejectPayload reject;
  reject.code = RejectCode::kTooBusy;
  reject.reason = "mempool full";

  auto encoded = make_reject_message(reject);
  CHECK(encoded.has_value());

  auto envelope = P2pMessage::deserialize(encoded->to_bytes());
  CHECK(envelope.has_value());

  auto decoded = parse_reject_message(*envelope);
  CHECK(decoded.has_value());
  CHECK(decoded->code == RejectCode::kTooBusy);
  CHECK(decoded->reason == "mempool full");
}

TEST_CASE("reject rejects unknown code") {
  RejectPayload reject;
  reject.code = RejectCode::kTooBusy;
  reject.reason = "ok";
  auto bytes = blockchain::net::serialize_reject(reject);
  CHECK(bytes.has_value());
  bytes->at(0) = std::byte{0xFF};  // corrupt code field

  auto decoded =
      blockchain::net::deserialize_reject(std::span<const std::byte>(bytes->data(), bytes->size()));
  CHECK(!decoded.has_value());
}

TEST_CASE("reject rejects oversized reason on serialize") {
  RejectPayload reject;
  reject.reason = std::string(static_cast<std::size_t>(kMaxRejectReasonBytes) + 1, 'r');
  CHECK(!blockchain::net::serialize_reject(reject).has_value());
}

TEST_CASE("ping and pong carry the same nonce on the wire") {
  constexpr std::uint64_t kNonce = 0x0123456789abcdefULL;
  PingPayload ping{.nonce = kNonce};

  auto ping_msg = make_ping_message(ping);
  CHECK(ping_msg.has_value());
  auto pong_msg = make_pong_message(ping);
  CHECK(pong_msg.has_value());

  auto ping_env = P2pMessage::deserialize(ping_msg->to_bytes());
  auto pong_env = P2pMessage::deserialize(pong_msg->to_bytes());
  CHECK(ping_env.has_value());
  CHECK(pong_env.has_value());

  auto got_ping = parse_ping_message(*ping_env);
  auto got_pong = parse_pong_message(*pong_env);
  CHECK(got_ping.has_value());
  CHECK(got_pong.has_value());
  CHECK_EQ(got_ping->nonce, kNonce);
  CHECK_EQ(got_pong->nonce, kNonce);
}

TEST_CASE("ping rejects wrong payload size") {
  auto ping_msg = make_ping_message(PingPayload{.nonce = 1});
  CHECK(ping_msg.has_value());
  ping_msg->payload.push_back(std::byte{0x00});

  CHECK(!parse_ping_message(*ping_msg).has_value());
}

TEST_CASE("parse_handshake rejects a ping envelope") {
  auto ping_msg = make_ping_message(PingPayload{.nonce = 99});
  CHECK(ping_msg.has_value());
  CHECK(!parse_handshake_message(*ping_msg).has_value());
}

TEST_CASE("block request payload round-trips on the wire") {
  auto encoded = make_block_request_message(BlockRequestPayload{.height = 7});
  CHECK(encoded.has_value());

  auto envelope = P2pMessage::deserialize(encoded->to_bytes());
  CHECK(envelope.has_value());
  CHECK(envelope->type == P2pMessageType::kBlockRequest);

  auto decoded = parse_block_request_message(*envelope);
  CHECK(decoded.has_value());
  CHECK_EQ(decoded->height, static_cast<std::uint32_t>(7));
}

TEST_CASE("block announce carries canonical genesis bytes") {
  NodeConfig config;
  config.genesis_timestamp = 42;
  const auto genesis = build_genesis_block(config);

  BlockAnnouncePayload payload;
  payload.block_bytes = genesis.to_bytes();

  auto encoded = make_block_announce_message(payload);
  CHECK(encoded.has_value());

  auto envelope = P2pMessage::deserialize(encoded->to_bytes());
  CHECK(envelope.has_value());

  auto decoded = parse_block_announce_message(*envelope);
  CHECK(decoded.has_value());
  CHECK_EQ(decoded->block_bytes.size(), payload.block_bytes.size());
}

TEST_CASE("tx announce rejects empty transaction bytes on serialize") {
  TxAnnouncePayload payload;
  CHECK(!blockchain::net::serialize_tx_announce(payload).has_value());
}
