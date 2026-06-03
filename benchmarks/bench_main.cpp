#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/transaction.hpp"
#include "blockchain/serialization/byte_io.hpp"

// Lightweight, dependency-free benchmark harness. Measures throughput of the
// hot paths flagged in the design doc. Not a regression gate yet; results are
// printed for manual/nightly comparison.

namespace {

using Clock = std::chrono::steady_clock;

template <typename Fn>
double measure_ns_per_op(std::size_t iterations, Fn&& fn) {
  const auto start = Clock::now();
  for (std::size_t i = 0; i < iterations; ++i) {
    fn(i);
  }
  const auto end = Clock::now();
  const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  return static_cast<double>(total_ns) / static_cast<double>(iterations);
}

blockchain::protocol::Transaction sample_tx() {
  blockchain::protocol::Transaction tx;
  blockchain::protocol::TxInput input;
  input.prevout.index = 1;
  tx.inputs.push_back(input);
  blockchain::protocol::TxOutput output;
  output.value = 5000;
  tx.outputs.push_back(output);
  return tx;
}

}  // namespace

int main() {
  constexpr std::size_t kIters = 200000;

  const std::string payload(1024, 'x');
  const auto bytes = std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload.data()),
                                                payload.size());

  volatile std::size_t sink = 0;

  const double sha_ns = measure_ns_per_op(kIters, [&](std::size_t) {
    sink += static_cast<std::size_t>(blockchain::crypto::sha256(bytes)[0]);
  });

  const auto tx = sample_tx();
  const double ser_ns = measure_ns_per_op(kIters, [&](std::size_t) {
    blockchain::serialization::ByteWriter writer;
    tx.serialize(writer);
    sink += writer.size();
  });

  const auto tx_bytes = tx.to_bytes();
  const double de_ns = measure_ns_per_op(kIters, [&](std::size_t) {
    blockchain::serialization::ByteReader reader(
        std::span<const std::byte>(tx_bytes.data(), tx_bytes.size()));
    auto decoded = blockchain::protocol::Transaction::deserialize(reader);
    sink += decoded.has_value() ? 1U : 0U;
  });

  std::cout << "benchmark (ns/op, lower is better)\n"
            << "  sha256(1 KiB):           " << sha_ns << "\n"
            << "  transaction serialize:   " << ser_ns << "\n"
            << "  transaction deserialize: " << de_ns << "\n";
  return sink == static_cast<std::size_t>(-1) ? 1 : 0;
}
