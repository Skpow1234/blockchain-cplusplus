#include "blockchain/net/p2p_payloads.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

#include "blockchain/serialization/byte_io.hpp"

namespace blockchain::net {
namespace {

[[nodiscard]] Result<void> put_node_id(serialization::ByteWriter& writer, const std::string& id) {
  if (id.empty()) {
    return make_error(ErrorCode::kInvalidMessage, "node_id must not be empty");
  }
  if (id.size() > kMaxNodeIdBytes) {
    return make_error(ErrorCode::kInvalidMessage, "node_id exceeds maximum length");
  }
  const auto bytes =
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(id.data()), id.size());
  writer.put_var_bytes(bytes);
  return {};
}

[[nodiscard]] Result<std::string> get_node_id(serialization::ByteReader& reader) {
  auto raw = reader.get_var_bytes(kMaxNodeIdBytes);
  if (!raw) {
    return std::unexpected(raw.error());
  }
  if (raw->empty()) {
    return make_error(ErrorCode::kInvalidMessage, "node_id must not be empty");
  }
  return std::string(reinterpret_cast<const char*>(raw->data()), raw->size());
}

[[nodiscard]] Result<void> put_reason(serialization::ByteWriter& writer,
                                      const std::string& reason) {
  if (reason.size() > kMaxRejectReasonBytes) {
    return make_error(ErrorCode::kInvalidMessage, "reject reason exceeds maximum length");
  }
  const auto bytes =
      std::span<const std::byte>(reinterpret_cast<const std::byte*>(reason.data()), reason.size());
  writer.put_var_bytes(bytes);
  return {};
}

[[nodiscard]] Result<std::string> get_reason(serialization::ByteReader& reader) {
  auto raw = reader.get_var_bytes(kMaxRejectReasonBytes);
  if (!raw) {
    return std::unexpected(raw.error());
  }
  return std::string(reinterpret_cast<const char*>(raw->data()), raw->size());
}

[[nodiscard]] Result<RejectCode> parse_reject_code(std::uint16_t raw) {
  switch (static_cast<RejectCode>(raw)) {
    case RejectCode::kProtocolMismatch:
    case RejectCode::kInvalidMessage:
    case RejectCode::kDuplicate:
    case RejectCode::kTooBusy:
    case RejectCode::kInternal:
      return static_cast<RejectCode>(raw);
  }
  return make_error(ErrorCode::kInvalidMessage, "unknown reject code: " + std::to_string(raw));
}

[[nodiscard]] Result<void> expect_exact_size(std::span<const std::byte> bytes,
                                             std::size_t expected) {
  if (bytes.size() != expected) {
    return make_error(ErrorCode::kInvalidMessage, "unexpected payload size");
  }
  return {};
}

template <typename T>
[[nodiscard]] Result<T> wrong_message_type() {
  return make_error(ErrorCode::kInvalidMessage, "unexpected P2P message type");
}

}  // namespace

constexpr std::string_view to_string(RejectCode code) noexcept {
  switch (code) {
    case RejectCode::kProtocolMismatch:
      return "ProtocolMismatch";
    case RejectCode::kInvalidMessage:
      return "InvalidMessage";
    case RejectCode::kDuplicate:
      return "Duplicate";
    case RejectCode::kTooBusy:
      return "TooBusy";
    case RejectCode::kInternal:
      return "Internal";
  }
  return "Unknown";
}

Result<std::vector<std::byte>> serialize_handshake(const HandshakePayload& payload) {
  serialization::ByteWriter writer;
  writer.put_u32(payload.network_magic);
  writer.put_u32(payload.height);
  writer.put_hash(payload.tip_hash);
  if (auto ok = put_node_id(writer, payload.node_id); !ok) {
    return std::unexpected(ok.error());
  }
  return writer.data();
}

Result<HandshakePayload> deserialize_handshake(std::span<const std::byte> bytes) {
  serialization::ByteReader reader(bytes);

  HandshakePayload payload;
  auto magic = reader.get_u32();
  if (!magic) {
    return std::unexpected(magic.error());
  }
  payload.network_magic = *magic;
  if (payload.network_magic != kNetworkMagic) {
    return make_error(ErrorCode::kInvalidMessage, "handshake network magic mismatch");
  }

  auto height = reader.get_u32();
  if (!height) {
    return std::unexpected(height.error());
  }
  payload.height = *height;

  auto tip = reader.get_hash();
  if (!tip) {
    return std::unexpected(tip.error());
  }
  payload.tip_hash = *tip;

  auto node_id = get_node_id(reader);
  if (!node_id) {
    return std::unexpected(node_id.error());
  }
  payload.node_id = std::move(*node_id);

  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return payload;
}

Result<P2pMessage> make_handshake_message(const HandshakePayload& payload) {
  auto bytes = serialize_handshake(payload);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  P2pMessage message;
  message.version = protocol::kProtocolVersion;
  message.type = P2pMessageType::kHandshake;
  message.payload = std::move(*bytes);
  return message;
}

Result<HandshakePayload> parse_handshake_message(const P2pMessage& message) {
  if (message.type != P2pMessageType::kHandshake) {
    return wrong_message_type<HandshakePayload>();
  }
  return deserialize_handshake(
      std::span<const std::byte>(message.payload.data(), message.payload.size()));
}

Result<std::vector<std::byte>> serialize_reject(const RejectPayload& payload) {
  serialization::ByteWriter writer;
  writer.put_u16(static_cast<std::uint16_t>(payload.code));
  if (auto ok = put_reason(writer, payload.reason); !ok) {
    return std::unexpected(ok.error());
  }
  return writer.data();
}

Result<RejectPayload> deserialize_reject(std::span<const std::byte> bytes) {
  serialization::ByteReader reader(bytes);

  auto code_raw = reader.get_u16();
  if (!code_raw) {
    return std::unexpected(code_raw.error());
  }
  auto code = parse_reject_code(*code_raw);
  if (!code) {
    return std::unexpected(code.error());
  }

  RejectPayload payload;
  payload.code = *code;

  auto reason = get_reason(reader);
  if (!reason) {
    return std::unexpected(reason.error());
  }
  payload.reason = std::move(*reason);

  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return payload;
}

Result<P2pMessage> make_reject_message(const RejectPayload& payload) {
  auto bytes = serialize_reject(payload);
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  P2pMessage message;
  message.version = protocol::kProtocolVersion;
  message.type = P2pMessageType::kReject;
  message.payload = std::move(*bytes);
  return message;
}

Result<RejectPayload> parse_reject_message(const P2pMessage& message) {
  if (message.type != P2pMessageType::kReject) {
    return wrong_message_type<RejectPayload>();
  }
  return deserialize_reject(
      std::span<const std::byte>(message.payload.data(), message.payload.size()));
}

std::vector<std::byte> serialize_ping(const PingPayload& payload) {
  serialization::ByteWriter writer;
  writer.put_u64(payload.nonce);
  return writer.data();
}

Result<PingPayload> deserialize_ping(std::span<const std::byte> bytes) {
  if (auto size_ok = expect_exact_size(bytes, sizeof(std::uint64_t)); !size_ok) {
    return std::unexpected(size_ok.error());
  }
  serialization::ByteReader reader(bytes);
  PingPayload payload;
  auto nonce = reader.get_u64();
  if (!nonce) {
    return std::unexpected(nonce.error());
  }
  payload.nonce = *nonce;
  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return payload;
}

Result<P2pMessage> make_ping_message(const PingPayload& payload) {
  P2pMessage message;
  message.version = protocol::kProtocolVersion;
  message.type = P2pMessageType::kPing;
  message.payload = serialize_ping(payload);
  return message;
}

Result<PingPayload> parse_ping_message(const P2pMessage& message) {
  if (message.type != P2pMessageType::kPing) {
    return wrong_message_type<PingPayload>();
  }
  return deserialize_ping(
      std::span<const std::byte>(message.payload.data(), message.payload.size()));
}

std::vector<std::byte> serialize_pong(const PongPayload& payload) {
  return serialize_ping(payload);
}

Result<PongPayload> deserialize_pong(std::span<const std::byte> bytes) {
  return deserialize_ping(bytes);
}

Result<P2pMessage> make_pong_message(const PongPayload& payload) {
  P2pMessage message;
  message.version = protocol::kProtocolVersion;
  message.type = P2pMessageType::kPong;
  message.payload = serialize_pong(payload);
  return message;
}

Result<PongPayload> parse_pong_message(const P2pMessage& message) {
  if (message.type != P2pMessageType::kPong) {
    return wrong_message_type<PongPayload>();
  }
  return deserialize_pong(
      std::span<const std::byte>(message.payload.data(), message.payload.size()));
}

}  // namespace blockchain::net
