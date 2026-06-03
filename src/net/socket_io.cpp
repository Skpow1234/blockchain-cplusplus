#include "blockchain/net/socket_io.hpp"

#include <array>
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "blockchain/serialization/byte_io.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
inline int socket_errno() {
  return WSAGetLastError();
}
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
inline int socket_errno() {
  return errno;
}
#endif

namespace blockchain::net {
namespace {

NativeSocket to_native(std::intptr_t handle) {
  return static_cast<NativeSocket>(handle);
}

std::intptr_t from_native(NativeSocket handle) {
  return static_cast<std::intptr_t>(handle);
}

bool is_valid(NativeSocket handle) {
  return handle != kInvalidSocket;
}

void close_native(NativeSocket handle) {
  if (!is_valid(handle)) {
    return;
  }
#ifdef _WIN32
  closesocket(handle);
#else
  close(handle);
#endif
}

[[nodiscard]] Error socket_error(std::string_view context) {
  return Error{ErrorCode::kPeerMisbehavior,
               std::string(context) + " (errno=" + std::to_string(socket_errno()) + ")"};
}

[[nodiscard]] bool recv_closed(const Error& err) noexcept {
  return err.code == ErrorCode::kPeerMisbehavior &&
         err.message.find("socket recv failed") != std::string::npos;
}

[[nodiscard]] Result<void> send_all_native(NativeSocket handle, std::span<const std::byte> data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto chunk = data.subspan(sent);
#ifdef _WIN32
    const int rc = send(handle, reinterpret_cast<const char*>(chunk.data()),
                        static_cast<int>(chunk.size()), 0);
#else
    const auto rc = send(handle, chunk.data(), chunk.size(), MSG_NOSIGNAL);
#endif
    if (rc <= 0) {
      return std::unexpected(socket_error("socket send failed"));
    }
    sent += static_cast<std::size_t>(rc);
  }
  return {};
}

[[nodiscard]] Result<void> recv_all_native(NativeSocket handle, std::span<std::byte> buffer) {
  std::size_t received = 0;
  while (received < buffer.size()) {
    const auto chunk = buffer.subspan(received);
#ifdef _WIN32
    const int rc =
        recv(handle, reinterpret_cast<char*>(chunk.data()), static_cast<int>(chunk.size()), 0);
#else
    const auto rc = recv(handle, chunk.data(), chunk.size(), 0);
#endif
    if (rc <= 0) {
      return std::unexpected(socket_error("socket recv failed"));
    }
    received += static_cast<std::size_t>(rc);
  }
  return {};
}

[[nodiscard]] Result<sockaddr_in> make_ipv4_address(const TcpEndpoint& endpoint) {
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(endpoint.port);
#ifdef _WIN32
  if (InetPtonA(AF_INET, endpoint.host.c_str(), &addr.sin_addr) != 1) {
#else
  if (inet_pton(AF_INET, endpoint.host.c_str(), &addr.sin_addr) != 1) {
#endif
    return make_error(ErrorCode::kInvalidConfig, "invalid IPv4 address: " + endpoint.host);
  }
  return addr;
}

}  // namespace

SocketLibrary::SocketLibrary()
    : initialized_(
#ifdef _WIN32
          [] {
            WSADATA data{};
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
          }()
#else
          [] {
            // Peers may close while we still send; default SIGPIPE would abort the process.
            (void)std::signal(SIGPIPE, SIG_IGN);
            return true;
          }()
#endif
      ) {
}

SocketLibrary::~SocketLibrary() {
  if (!initialized_) {
    return;
  }
#ifdef _WIN32
  WSACleanup();
#endif
}

Result<TcpEndpoint> parse_tcp_endpoint(std::string_view text) {
  const std::size_t colon = text.rfind(':');
  if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
    return make_error(ErrorCode::kInvalidConfig, "peer must be host:port");
  }
  TcpEndpoint endpoint;
  endpoint.host = std::string(text.substr(0, colon));
  auto port =
      std::from_chars(text.data() + static_cast<std::ptrdiff_t>(colon + 1),
                      text.data() + static_cast<std::ptrdiff_t>(text.size()), endpoint.port);
  if (port.ec != std::errc{} || port.ptr != text.data() + text.size()) {
    return make_error(ErrorCode::kInvalidConfig, "invalid peer port");
  }
  if (endpoint.port == 0) {
    return make_error(ErrorCode::kInvalidConfig, "peer port must not be zero");
  }
  return endpoint;
}

TcpSocket::TcpSocket(int handle) noexcept : handle_(handle) {}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : handle_(other.handle_) {
  other.handle_ = -1;
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = -1;
  }
  return *this;
}

TcpSocket::~TcpSocket() {
  close();
}

bool TcpSocket::valid() const noexcept {
  return handle_ >= 0 && is_valid(to_native(handle_));
}

void TcpSocket::close() noexcept {
  if (handle_ >= 0) {
    close_native(to_native(handle_));
    handle_ = -1;
  }
}

Result<TcpSocket> TcpSocket::connect(const TcpEndpoint& endpoint) {
  auto addr = make_ipv4_address(endpoint);
  if (!addr) {
    return std::unexpected(addr.error());
  }

  const NativeSocket handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!is_valid(handle)) {
    return std::unexpected(socket_error("socket creation failed"));
  }

  if (::connect(handle, reinterpret_cast<sockaddr*>(&*addr), sizeof(sockaddr_in)) != 0) {
    close_native(handle);
    return std::unexpected(socket_error("connect failed"));
  }

  return TcpSocket(static_cast<int>(from_native(handle)));
}

Result<void> TcpSocket::send_raw(std::span<const std::byte> data) const {
  if (!valid()) {
    return make_error(ErrorCode::kPeerMisbehavior, "socket is not connected");
  }
  if (data.empty()) {
    return {};
  }
  return send_all_native(to_native(handle_), data);
}

Result<void> TcpSocket::send_frame_length_prefix(std::uint32_t len) const {
  std::array<std::byte, 4> len_bytes{};
  for (std::size_t i = 0; i < len_bytes.size(); ++i) {
    len_bytes[i] = static_cast<std::byte>((len >> (8U * i)) & 0xFFU);
  }
  return send_raw(len_bytes);
}

Result<void> TcpSocket::send_framed(std::span<const std::byte> body) const {
  if (!valid()) {
    return make_error(ErrorCode::kPeerMisbehavior, "socket is not connected");
  }
  if (body.size() > kMaxP2pFrameBytes) {
    return make_error(ErrorCode::kResourceLimitExceeded, "frame exceeds maximum size");
  }

  std::array<std::byte, 4> len_bytes{};
  const auto len = static_cast<std::uint32_t>(body.size());
  for (std::size_t i = 0; i < len_bytes.size(); ++i) {
    len_bytes[i] = static_cast<std::byte>((len >> (8U * i)) & 0xFFU);
  }

  if (auto ok = send_all_native(to_native(handle_), len_bytes); !ok) {
    return ok;
  }
  if (body.empty()) {
    return {};
  }
  return send_all_native(to_native(handle_), body);
}

Result<std::vector<std::byte>> TcpSocket::recv_framed(std::uint32_t max_frame_bytes) const {
  if (!valid()) {
    return make_error(ErrorCode::kPeerMisbehavior, "socket is not connected");
  }

  std::array<std::byte, 4> len_bytes{};
  if (auto ok = recv_all_native(to_native(handle_), len_bytes); !ok) {
    return std::unexpected(ok.error());
  }

  std::uint32_t len = 0;
  for (std::size_t i = 0; i < len_bytes.size(); ++i) {
    len |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(len_bytes[i])) << (8U * i);
  }
  if (len > max_frame_bytes) {
    return make_error(ErrorCode::kResourceLimitExceeded, "frame length exceeds maximum");
  }

  std::vector<std::byte> body(len);
  if (len > 0) {
    if (auto ok = recv_all_native(to_native(handle_), body); !ok) {
      if (recv_closed(ok.error())) {
        return make_error(ErrorCode::kInvalidMessage, "incomplete frame body");
      }
      return std::unexpected(ok.error());
    }
  }
  return body;
}

TcpListener::TcpListener(int handle) noexcept : handle_(handle) {}

TcpListener::TcpListener(TcpListener&& other) noexcept : handle_(other.handle_) {
  other.handle_ = -1;
}

TcpListener& TcpListener::operator=(TcpListener&& other) noexcept {
  if (this != &other) {
    close();
    handle_ = other.handle_;
    other.handle_ = -1;
  }
  return *this;
}

TcpListener::~TcpListener() {
  close();
}

void TcpListener::close() noexcept {
  if (handle_ >= 0) {
    close_native(to_native(handle_));
    handle_ = -1;
  }
}

Result<TcpListener> TcpListener::bind(const TcpEndpoint& endpoint) {
  auto addr = make_ipv4_address(endpoint);
  if (!addr) {
    return std::unexpected(addr.error());
  }

  const NativeSocket handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (!is_valid(handle)) {
    return std::unexpected(socket_error("socket creation failed"));
  }

  int reuse = 1;
  setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
             sizeof(reuse));

  if (::bind(handle, reinterpret_cast<sockaddr*>(&*addr), sizeof(sockaddr_in)) != 0) {
    close_native(handle);
    return std::unexpected(socket_error("bind failed"));
  }
  constexpr int kListenBacklog = 8;
  if (listen(handle, kListenBacklog) != 0) {
    close_native(handle);
    return std::unexpected(socket_error("listen failed"));
  }

  return TcpListener(static_cast<int>(from_native(handle)));
}

Result<std::uint16_t> TcpListener::bound_port() const {
  if (handle_ < 0) {
    return make_error(ErrorCode::kPeerMisbehavior, "listener is not bound");
  }
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (getsockname(to_native(handle_), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    return std::unexpected(socket_error("getsockname failed"));
  }
  return ntohs(addr.sin_port);
}

Result<TcpSocket> TcpListener::accept() const {
  if (handle_ < 0) {
    return make_error(ErrorCode::kPeerMisbehavior, "listener is not bound");
  }
  const NativeSocket client = ::accept(to_native(handle_), nullptr, nullptr);
  if (!is_valid(client)) {
    return std::unexpected(socket_error("accept failed"));
  }
  return TcpSocket(static_cast<int>(from_native(client)));
}

Result<void> send_message(TcpSocket& socket, const P2pMessage& message) {
  const std::vector<std::byte> wire = message.to_bytes();
  return socket.send_framed(std::span<const std::byte>(wire.data(), wire.size()));
}

Result<P2pMessage> recv_message(TcpSocket& socket) {
  auto frame = socket.recv_framed(kMaxP2pFrameBytes);
  if (!frame) {
    return std::unexpected(frame.error());
  }
  return P2pMessage::deserialize(std::span<const std::byte>(frame->data(), frame->size()));
}

}  // namespace blockchain::net
