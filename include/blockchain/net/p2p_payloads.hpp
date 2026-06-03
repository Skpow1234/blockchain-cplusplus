#ifndef BLOCKCHAIN_NET_P2P_PAYLOADS_HPP
#define BLOCKCHAIN_NET_P2P_PAYLOADS_HPP

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"
#include "blockchain/net/p2p_message.hpp"
#include "blockchain/protocol/constants.hpp"

// Typed P2P message payloads. Each type has a documented binary layout, canonical
// serialization, and validation separate from the envelope (p2p_message.hpp).
// Higher layers wrap these bytes in a P2pMessage before sending on the wire.
namespace blockchain::net {

inline constexpr std::uint32_t kMaxNodeIdBytes = 64;
inline constexpr std::uint32_t kMaxRejectReasonBytes = 256;

// ---------------------------------------------------------------------------
// Handshake — exchanged immediately after a TCP connection is established.
// Layout: network_magic u32 | height u32 | tip_hash 32 bytes | node_id (u32 len
// + bytes, max kMaxNodeIdBytes).
// ---------------------------------------------------------------------------
struct HandshakePayload {
  std::uint32_t network_magic = kNetworkMagic;
  std::uint32_t height = 0;
  crypto::Hash256 tip_hash{};
  std::string node_id;
};

[[nodiscard]] Result<std::vector<std::byte>> serialize_handshake(const HandshakePayload& payload);
[[nodiscard]] Result<HandshakePayload> deserialize_handshake(std::span<const std::byte> bytes);

[[nodiscard]] Result<P2pMessage> make_handshake_message(const HandshakePayload& payload);
[[nodiscard]] Result<HandshakePayload> parse_handshake_message(const P2pMessage& message);

// ---------------------------------------------------------------------------
// Reject — peer error response with a stable code and bounded reason string.
// Layout: code u16 | reason (u32 len + bytes, max kMaxRejectReasonBytes).
// ---------------------------------------------------------------------------
enum class RejectCode : std::uint16_t {
  kProtocolMismatch = 1,
  kInvalidMessage,
  kDuplicate,
  kTooBusy,
  kInternal,
};

[[nodiscard]] constexpr std::string_view to_string(RejectCode code) noexcept;

struct RejectPayload {
  RejectCode code = RejectCode::kInvalidMessage;
  std::string reason;
};

[[nodiscard]] Result<std::vector<std::byte>> serialize_reject(const RejectPayload& payload);
[[nodiscard]] Result<RejectPayload> deserialize_reject(std::span<const std::byte> bytes);

[[nodiscard]] Result<P2pMessage> make_reject_message(const RejectPayload& payload);
[[nodiscard]] Result<RejectPayload> parse_reject_message(const P2pMessage& message);

// ---------------------------------------------------------------------------
// Ping / Pong — keep-alive with a caller-chosen nonce echoed by pong.
// Layout: nonce u64 (exactly 8 bytes).
// ---------------------------------------------------------------------------
struct PingPayload {
  std::uint64_t nonce = 0;
};

[[nodiscard]] std::vector<std::byte> serialize_ping(const PingPayload& payload);
[[nodiscard]] Result<PingPayload> deserialize_ping(std::span<const std::byte> bytes);

[[nodiscard]] Result<P2pMessage> make_ping_message(const PingPayload& payload);
[[nodiscard]] Result<PingPayload> parse_ping_message(const P2pMessage& message);

using PongPayload = PingPayload;

[[nodiscard]] std::vector<std::byte> serialize_pong(const PongPayload& payload);
[[nodiscard]] Result<PongPayload> deserialize_pong(std::span<const std::byte> bytes);

[[nodiscard]] Result<P2pMessage> make_pong_message(const PongPayload& payload);
[[nodiscard]] Result<PongPayload> parse_pong_message(const P2pMessage& message);

}  // namespace blockchain::net

#endif  // BLOCKCHAIN_NET_P2P_PAYLOADS_HPP
