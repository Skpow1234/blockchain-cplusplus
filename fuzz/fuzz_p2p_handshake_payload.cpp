#include <cstddef>
#include <cstdint>
#include <span>

#include "blockchain/net/p2p_payloads.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  auto payload = blockchain::net::deserialize_handshake(bytes);
  if (payload.has_value()) {
    const auto reencoded = blockchain::net::serialize_handshake(*payload);
    (void)reencoded;
  }
  return 0;
}
