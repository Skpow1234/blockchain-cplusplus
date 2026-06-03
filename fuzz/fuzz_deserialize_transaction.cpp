#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"

// libFuzzer entry point. Goal: parsing arbitrary bytes must never crash, leak,
// hit UB, or allocate without bound. Accepted inputs must re-serialize cleanly.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  blockchain::serialization::ByteReader reader(bytes);

  auto tx = blockchain::protocol::Transaction::deserialize(reader);
  if (tx.has_value()) {
    const std::vector<std::byte> reencoded = tx->to_bytes();
    (void)reencoded;
    (void)tx->txid();
  }
  return 0;
}
