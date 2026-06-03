#ifndef BLOCKCHAIN_NET_P2P_MESSAGE_HPP
#define BLOCKCHAIN_NET_P2P_MESSAGE_HPP

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "blockchain/error.hpp"

// P2P wire messages for the local network layer. This module owns the message
// envelope only: parsing, canonical serialization, integrity checks, and size
// limits. Payload semantics (handshake fields, transaction bytes, etc.) are
// validated by the modules that understand each message type.
//
// Binary layout (little-endian, canonical, no trailing garbage):
//   magic          u32   kNetworkMagic — identifies the network
//   version        u32   must equal kProtocolVersion for now
//   type           u16   P2pMessageType
//   payload_len    u32   byte length of payload (may be 0)
//   payload        [payload_len] opaque bytes
//   checksum       u32   first 4 bytes of SHA-256(magic|version|type|len|payload)
//
// Invariants:
//   * payload_len <= kMaxP2pPayloadBytes before any allocation
//   * Unknown message types and bad checksums are rejected
//   * Deserialization consumes the entire input span (no trailing bytes)
namespace blockchain::net {

// Documented network identifier (not a runtime secret). Distinct magic values
// will be assigned per testnet when public networks are introduced.
inline constexpr std::uint32_t kNetworkMagic = 0xBC173C01U;

// Largest allowed payload. Sized to admit a full block; enforced before
// allocating the payload buffer.
inline constexpr std::uint32_t kMaxP2pPayloadBytes = 1U << 20U;  // 1 MiB

// Fixed header size excluding the variable payload.
inline constexpr std::size_t kP2pEnvelopeOverhead = 4U + 4U + 2U + 4U + 4U;  // 18 bytes

enum class P2pMessageType : std::uint16_t {
  kHandshake = 1,
  kPing,
  kPong,
  kPeerInfo,
  kTxAnnounce,
  kTxRequest,
  kTxResponse,
  kBlockAnnounce,
  kBlockRequest,
  kBlockResponse,
  kReject,
};

[[nodiscard]] constexpr bool is_known_p2p_message_type(P2pMessageType type) noexcept {
  switch (type) {
    case P2pMessageType::kHandshake:
    case P2pMessageType::kPing:
    case P2pMessageType::kPong:
    case P2pMessageType::kPeerInfo:
    case P2pMessageType::kTxAnnounce:
    case P2pMessageType::kTxRequest:
    case P2pMessageType::kTxResponse:
    case P2pMessageType::kBlockAnnounce:
    case P2pMessageType::kBlockRequest:
    case P2pMessageType::kBlockResponse:
    case P2pMessageType::kReject:
      return true;
  }
  return false;
}

[[nodiscard]] constexpr std::string_view to_string(P2pMessageType type) noexcept {
  switch (type) {
    case P2pMessageType::kHandshake:
      return "Handshake";
    case P2pMessageType::kPing:
      return "Ping";
    case P2pMessageType::kPong:
      return "Pong";
    case P2pMessageType::kPeerInfo:
      return "PeerInfo";
    case P2pMessageType::kTxAnnounce:
      return "TxAnnounce";
    case P2pMessageType::kTxRequest:
      return "TxRequest";
    case P2pMessageType::kTxResponse:
      return "TxResponse";
    case P2pMessageType::kBlockAnnounce:
      return "BlockAnnounce";
    case P2pMessageType::kBlockRequest:
      return "BlockRequest";
    case P2pMessageType::kBlockResponse:
      return "BlockResponse";
    case P2pMessageType::kReject:
      return "Reject";
  }
  return "Unknown";
}

struct P2pMessage {
  std::uint32_t version = 0;
  P2pMessageType type = P2pMessageType::kPing;
  std::vector<std::byte> payload;

  // Serializes to the canonical wire form including checksum.
  [[nodiscard]] std::vector<std::byte> to_bytes() const;

  // Parses and validates a wire message. Returns kParseError on truncation,
  // kInvalidMessage for magic/checksum/type violations, and
  // kUnsupportedVersion when the version field is not supported.
  [[nodiscard]] static Result<P2pMessage> deserialize(std::span<const std::byte> wire);
};

}  // namespace blockchain::net

#endif  // BLOCKCHAIN_NET_P2P_MESSAGE_HPP
