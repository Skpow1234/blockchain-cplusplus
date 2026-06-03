#ifndef BLOCKCHAIN_SERIALIZATION_BYTE_IO_HPP
#define BLOCKCHAIN_SERIALIZATION_BYTE_IO_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"

namespace blockchain::serialization {

// Canonical wire encoding used throughout the project.
//
// Layout rules (documented invariant):
//   * Integers are fixed-width, little-endian, unsigned.
//   * Hashes are 32 raw bytes, in stored order.
//   * Variable-length byte strings and vectors are prefixed with a u32 count.
//   * There is exactly one valid encoding for any value (canonical form).
//   * Deserialization is bounds-checked; readers never over-read their input.
//
// Length-prefixed reads require an explicit maximum so that a hostile peer
// cannot trigger an unbounded allocation by claiming a huge count.

class ByteWriter {
 public:
  void put_u8(std::uint8_t value);
  void put_u16(std::uint16_t value);
  void put_u32(std::uint32_t value);
  void put_u64(std::uint64_t value);
  void put_hash(const crypto::Hash256& hash);
  void put_bytes(std::span<const std::byte> bytes);

  // Writes a u32 length prefix followed by the raw bytes.
  void put_var_bytes(std::span<const std::byte> bytes);

  [[nodiscard]] const std::vector<std::byte>& data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return buffer_.size(); }

 private:
  std::vector<std::byte> buffer_;
};

class ByteReader {
 public:
  explicit ByteReader(std::span<const std::byte> data) noexcept : data_(data) {}

  [[nodiscard]] Result<std::uint8_t> get_u8();
  [[nodiscard]] Result<std::uint16_t> get_u16();
  [[nodiscard]] Result<std::uint32_t> get_u32();
  [[nodiscard]] Result<std::uint64_t> get_u64();
  [[nodiscard]] Result<crypto::Hash256> get_hash();

  // Reads a fixed number of raw bytes.
  [[nodiscard]] Result<std::vector<std::byte>> get_bytes(std::size_t count);

  // Reads a u32-length-prefixed byte string, rejecting any length above
  // max_len before allocating.
  [[nodiscard]] Result<std::vector<std::byte>> get_var_bytes(std::uint32_t max_len);

  // Reads a u32 length prefix, validating it against max_count. Useful for
  // bounding element counts before reserving container capacity.
  [[nodiscard]] Result<std::uint32_t> get_length(std::uint32_t max_count);

  [[nodiscard]] std::size_t remaining() const noexcept { return data_.size() - pos_; }
  [[nodiscard]] bool at_end() const noexcept { return pos_ == data_.size(); }

  // Enforces the "reject trailing garbage" rule: succeeds only when the input
  // has been fully consumed.
  [[nodiscard]] Result<void> expect_end() const;

 private:
  [[nodiscard]] Result<void> ensure(std::size_t count) const;

  std::span<const std::byte> data_;
  std::size_t pos_ = 0;
};

}  // namespace blockchain::serialization

#endif  // BLOCKCHAIN_SERIALIZATION_BYTE_IO_HPP
