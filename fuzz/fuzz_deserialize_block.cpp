#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/protocol/block.hpp"
#include "blockchain/serialization/byte_io.hpp"

// libFuzzer entry point for block deserialization. See the transaction fuzzer
// for the shared goals (no crashes, no UB, bounded allocation).
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  blockchain::serialization::ByteReader reader(bytes);

  auto block = blockchain::protocol::Block::deserialize(reader);
  if (block.has_value()) {
    const std::vector<std::byte> reencoded = block->to_bytes();
    (void)reencoded;
    (void)block->header.hash();
  }
  return 0;
}
