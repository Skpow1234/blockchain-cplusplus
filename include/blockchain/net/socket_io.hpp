#ifndef BLOCKCHAIN_NET_SOCKET_IO_HPP
#define BLOCKCHAIN_NET_SOCKET_IO_HPP

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/net/p2p_message.hpp"

// Blocking TCP sockets with length-prefixed framing for P2P messages.
//
// Frame layout on the wire (separate from the P2P message envelope):
//   frame_len u32  byte length of the following P2P message bytes
//   message    [frame_len]  output of P2pMessage::to_bytes()
//
// SocketLibrary must be constructed once per process before any socket calls
// (WSAStartup on Windows). Networking is kept separate from consensus logic.
namespace blockchain::net {

struct TcpEndpoint {
  std::string host;
  std::uint16_t port = 0;
};

// Parses "host:port" (IPv4 hostnames or dotted-quad only at this stage).
[[nodiscard]] Result<TcpEndpoint> parse_tcp_endpoint(std::string_view text);

// Largest framed message: full P2P envelope at maximum payload size.
inline constexpr std::uint32_t kMaxP2pFrameBytes =
    kMaxP2pPayloadBytes + static_cast<std::uint32_t>(kP2pEnvelopeOverhead);

// Initializes the platform socket library for the lifetime of this object.
class SocketLibrary {
 public:
  SocketLibrary();
  ~SocketLibrary();

  SocketLibrary(const SocketLibrary&) = delete;
  SocketLibrary& operator=(const SocketLibrary&) = delete;
  SocketLibrary(SocketLibrary&&) = delete;
  SocketLibrary& operator=(SocketLibrary&&) = delete;

 private:
  bool initialized_ = false;
};

class TcpSocket {
 public:
  TcpSocket() = default;
  TcpSocket(TcpSocket&& other) noexcept;
  TcpSocket& operator=(TcpSocket&& other) noexcept;
  ~TcpSocket();

  TcpSocket(const TcpSocket&) = delete;
  TcpSocket& operator=(const TcpSocket&) = delete;

  [[nodiscard]] static Result<TcpSocket> connect(const TcpEndpoint& endpoint);

  [[nodiscard]] bool valid() const noexcept;

  // Sends a length-prefixed frame (u32 little-endian + body).
  [[nodiscard]] Result<void> send_framed(std::span<const std::byte> body) const;

  // Receives a length-prefixed frame. Rejects lengths above max_frame_bytes
  // before allocating the body buffer.
  [[nodiscard]] Result<std::vector<std::byte>> recv_framed(
      std::uint32_t max_frame_bytes = kMaxP2pFrameBytes) const;

  void close() noexcept;

  int handle_ = -1;

 private:
  explicit TcpSocket(int handle) noexcept;

  friend class TcpListener;
};

class TcpListener {
 public:
  TcpListener() = default;
  TcpListener(TcpListener&& other) noexcept;
  TcpListener& operator=(TcpListener&& other) noexcept;
  ~TcpListener();

  TcpListener(const TcpListener&) = delete;
  TcpListener& operator=(const TcpListener&) = delete;

  // Binds to endpoint.host:endpoint.port (use port 0 for an ephemeral port).
  [[nodiscard]] static Result<TcpListener> bind(const TcpEndpoint& endpoint);

  // Returns the port actually bound (after bind with port 0).
  [[nodiscard]] Result<std::uint16_t> bound_port() const;

  [[nodiscard]] Result<TcpSocket> accept() const;

  void close() noexcept;

 private:
  explicit TcpListener(int handle) noexcept;

  int handle_ = -1;
};

// Sends a serialized P2P message inside a length-prefixed frame.
[[nodiscard]] Result<void> send_message(TcpSocket& socket, const P2pMessage& message);

// Receives and deserializes a framed P2P message.
[[nodiscard]] Result<P2pMessage> recv_message(TcpSocket& socket);

}  // namespace blockchain::net

#endif  // BLOCKCHAIN_NET_SOCKET_IO_HPP
