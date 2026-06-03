#include <cstddef>
#include <cstdint>
#include <span>

#include "blockchain/net/p2p_message.hpp"

// libFuzzer entry point. Parsing arbitrary bytes must never crash, leak, hit UB,
// or allocate without bound.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  auto message = blockchain::net::P2pMessage::deserialize(bytes);
  if (message.has_value()) {
    const std::vector<std::byte> reencoded = message->to_bytes();
    (void)reencoded;
  }
  return 0;
}
