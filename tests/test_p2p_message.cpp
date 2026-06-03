#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::net::kMaxP2pPayloadBytes;
using blockchain::net::kNetworkMagic;
using blockchain::net::kP2pEnvelopeOverhead;
using blockchain::net::P2pMessage;
using blockchain::net::P2pMessageType;
using blockchain::protocol::kProtocolVersion;
using blockchain::serialization::ByteWriter;

namespace {

P2pMessage make_message(P2pMessageType type, std::vector<std::byte> payload) {
  P2pMessage message;
  message.version = kProtocolVersion;
  message.type = type;
  message.payload = std::move(payload);
  return message;
}

}  // namespace

TEST_CASE("empty ping round-trips on the wire") {
  const P2pMessage original = make_message(P2pMessageType::kPing, {});
  const std::vector<std::byte> wire = original.to_bytes();
  CHECK_EQ(wire.size(), kP2pEnvelopeOverhead);

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(decoded.has_value());
  CHECK_EQ(decoded->version, kProtocolVersion);
  CHECK(decoded->type == P2pMessageType::kPing);
  CHECK(decoded->payload.empty());
}

TEST_CASE("payload bytes round-trip") {
  const std::vector<std::byte> payload = {std::byte{0xDE}, std::byte{0xAD}, std::byte{0xBE},
                                          std::byte{0xEF}};
  const P2pMessage original = make_message(P2pMessageType::kTxAnnounce, payload);
  const std::vector<std::byte> wire = original.to_bytes();

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(decoded.has_value());
  CHECK(decoded->payload == payload);
  CHECK(decoded->type == P2pMessageType::kTxAnnounce);
}

TEST_CASE("wrong magic is rejected") {
  P2pMessage message = make_message(P2pMessageType::kPing, {});
  std::vector<std::byte> wire = message.to_bytes();
  wire[0] = std::byte{0xFF};

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kInvalidMessage);
}

TEST_CASE("unsupported protocol version is rejected") {
  P2pMessage message = make_message(P2pMessageType::kPing, {});
  message.version = kProtocolVersion + 1;
  const std::vector<std::byte> wire = message.to_bytes();

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kUnsupportedVersion);
}

TEST_CASE("checksum mismatch is rejected") {
  const std::vector<std::byte> wire = make_message(P2pMessageType::kPong, {}).to_bytes();
  std::vector<std::byte> tampered = wire;
  tampered.back() = std::byte{0x00};  // corrupt checksum byte

  auto decoded = P2pMessage::deserialize(tampered);
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kInvalidMessage);
}

TEST_CASE("trailing garbage is rejected") {
  std::vector<std::byte> wire = make_message(P2pMessageType::kPing, {}).to_bytes();
  wire.push_back(std::byte{0x42});

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kParseError);
}

TEST_CASE("unknown message type is rejected") {
  ByteWriter writer;
  writer.put_u32(kNetworkMagic);
  writer.put_u32(kProtocolVersion);
  writer.put_u16(0xFFFF);  // not a known type
  writer.put_u32(0);
  writer.put_u32(0);  // placeholder checksum; parse fails on type before checksum

  auto decoded = P2pMessage::deserialize(
      std::span<const std::byte>(writer.data().data(), writer.data().size()));
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kInvalidMessage);
}

TEST_CASE("oversized payload length is rejected without large allocation") {
  ByteWriter writer;
  writer.put_u32(kNetworkMagic);
  writer.put_u32(kProtocolVersion);
  writer.put_u16(static_cast<std::uint16_t>(P2pMessageType::kBlockResponse));
  writer.put_u32(kMaxP2pPayloadBytes + 1);
  writer.put_u32(0);

  auto decoded = P2pMessage::deserialize(
      std::span<const std::byte>(writer.data().data(), writer.data().size()));
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kParseError);
}

TEST_CASE("truncated message is rejected") {
  std::vector<std::byte> wire = make_message(P2pMessageType::kHandshake, {}).to_bytes();
  wire.resize(wire.size() - 1);

  auto decoded = P2pMessage::deserialize(wire);
  CHECK(!decoded.has_value());
  CHECK(decoded.error().code == ErrorCode::kParseError);
}
