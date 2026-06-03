#include <cstddef>
#include <cstdint>
#include <vector>

#include "blockchain/storage/chain_store.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(data), size);
  (void)blockchain::storage::ChainStore::decode_ledger(bytes);
  return 0;
}
