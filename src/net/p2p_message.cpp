#include "blockchain/net/p2p_message.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"

namespace blockchain::net {
namespace {

// First four bytes of SHA-256(data), interpreted as a little-endian u32.
[[nodiscard]] std::uint32_t checksum_of(std::span<const std::byte> data) {
  const crypto::Hash256 digest = crypto::sha256(data);
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(digest[i])) << (8U * i);
  }
  return value;
}

void write_header_body(serialization::ByteWriter& writer, const P2pMessage& message) {
  writer.put_u32(kNetworkMagic);
  writer.put_u32(message.version);
  writer.put_u16(static_cast<std::uint16_t>(message.type));
  writer.put_u32(static_cast<std::uint32_t>(message.payload.size()));
  if (!message.payload.empty()) {
    writer.put_bytes(std::span<const std::byte>(message.payload.data(), message.payload.size()));
  }
}

[[nodiscard]] Result<P2pMessageType> parse_message_type(std::uint16_t raw) {
  const auto type = static_cast<P2pMessageType>(raw);
  if (!is_known_p2p_message_type(type)) {
    return make_error(ErrorCode::kInvalidMessage,
                      "unknown P2P message type: " + std::to_string(raw));
  }
  return type;
}

}  // namespace

std::vector<std::byte> P2pMessage::to_bytes() const {
  serialization::ByteWriter writer;
  write_header_body(writer, *this);
  writer.put_u32(checksum_of(
      std::span<const std::byte>(writer.data().data(), writer.data().size())));
  return writer.data();
}

Result<P2pMessage> P2pMessage::deserialize(std::span<const std::byte> wire) {
  if (wire.size() < kP2pEnvelopeOverhead) {
    return make_error(ErrorCode::kParseError, "P2P message too short");
  }

  serialization::ByteReader reader(wire);

  auto magic = reader.get_u32();
  if (!magic) {
    return std::unexpected(magic.error());
  }
  if (*magic != kNetworkMagic) {
    return make_error(ErrorCode::kInvalidMessage, "unexpected network magic");
  }

  auto version = reader.get_u32();
  if (!version) {
    return std::unexpected(version.error());
  }
  if (*version != protocol::kProtocolVersion) {
    return make_error(ErrorCode::kUnsupportedVersion, "unsupported P2P protocol version");
  }

  auto type_raw = reader.get_u16();
  if (!type_raw) {
    return std::unexpected(type_raw.error());
  }
  auto type = parse_message_type(*type_raw);
  if (!type) {
    return std::unexpected(type.error());
  }

  auto payload_len = reader.get_length(kMaxP2pPayloadBytes);
  if (!payload_len) {
    return std::unexpected(payload_len.error());
  }

  std::vector<std::byte> payload;
  if (*payload_len > 0) {
    auto bytes = reader.get_bytes(static_cast<std::size_t>(*payload_len));
    if (!bytes) {
      return std::unexpected(bytes.error());
    }
    payload = std::move(*bytes);
  }

  auto checksum = reader.get_u32();
  if (!checksum) {
    return std::unexpected(checksum.error());
  }

  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }

  // Recompute checksum over the header+payload prefix (everything before the
  // checksum field).
  const std::size_t body_size = wire.size() - sizeof(std::uint32_t);
  const std::uint32_t expected = checksum_of(wire.subspan(0, body_size));
  if (*checksum != expected) {
    return make_error(ErrorCode::kInvalidMessage, "P2P message checksum mismatch");
  }

  P2pMessage message;
  message.version = *version;
  message.type = *type;
  message.payload = std::move(payload);
  return message;
}

}  // namespace blockchain::net
