#include "blockchain/serialization/byte_io.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace blockchain::serialization {

void ByteWriter::put_u8(std::uint8_t value) {
  buffer_.push_back(static_cast<std::byte>(value));
}

void ByteWriter::put_u16(std::uint16_t value) {
  buffer_.push_back(static_cast<std::byte>(value & 0xffU));
  buffer_.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void ByteWriter::put_u32(std::uint32_t value) {
  for (unsigned shift = 0; shift < 32U; shift += 8U) {
    buffer_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

void ByteWriter::put_u64(std::uint64_t value) {
  for (unsigned shift = 0; shift < 64U; shift += 8U) {
    buffer_.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

void ByteWriter::put_hash(const crypto::Hash256& hash) {
  buffer_.insert(buffer_.end(), hash.begin(), hash.end());
}

void ByteWriter::put_bytes(std::span<const std::byte> bytes) {
  buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

void ByteWriter::put_var_bytes(std::span<const std::byte> bytes) {
  put_u32(static_cast<std::uint32_t>(bytes.size()));
  put_bytes(bytes);
}

Result<void> ByteReader::ensure(std::size_t count) const {
  if (count > remaining()) {
    return make_error(ErrorCode::kParseError,
                      "unexpected end of input: need " + std::to_string(count) + " byte(s), have " +
                          std::to_string(remaining()));
  }
  return {};
}

Result<std::uint8_t> ByteReader::get_u8() {
  if (auto ok = ensure(1); !ok) {
    return std::unexpected(ok.error());
  }
  return static_cast<std::uint8_t>(data_[pos_++]);
}

Result<std::uint16_t> ByteReader::get_u16() {
  if (auto ok = ensure(2); !ok) {
    return std::unexpected(ok.error());
  }
  std::uint16_t value = 0;
  value |= static_cast<std::uint16_t>(static_cast<std::uint8_t>(data_[pos_]));
  value |= static_cast<std::uint16_t>(static_cast<std::uint16_t>(static_cast<std::uint8_t>(
                                          data_[pos_ + 1]))
                                      << 8U);
  pos_ += 2;
  return value;
}

Result<std::uint32_t> ByteReader::get_u32() {
  if (auto ok = ensure(4); !ok) {
    return std::unexpected(ok.error());
  }
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4U; ++i) {
    value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data_[pos_ + i])) << (i * 8U);
  }
  pos_ += 4;
  return value;
}

Result<std::uint64_t> ByteReader::get_u64() {
  if (auto ok = ensure(8); !ok) {
    return std::unexpected(ok.error());
  }
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8U; ++i) {
    value |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(data_[pos_ + i])) << (i * 8U);
  }
  pos_ += 8;
  return value;
}

Result<crypto::Hash256> ByteReader::get_hash() {
  if (auto ok = ensure(crypto::Hash256{}.size()); !ok) {
    return std::unexpected(ok.error());
  }
  crypto::Hash256 hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    hash[i] = data_[pos_ + i];
  }
  pos_ += hash.size();
  return hash;
}

Result<std::vector<std::byte>> ByteReader::get_bytes(std::size_t count) {
  if (auto ok = ensure(count); !ok) {
    return std::unexpected(ok.error());
  }
  std::vector<std::byte> out(data_.begin() + static_cast<std::ptrdiff_t>(pos_),
                             data_.begin() + static_cast<std::ptrdiff_t>(pos_ + count));
  pos_ += count;
  return out;
}

Result<std::uint32_t> ByteReader::get_length(std::uint32_t max_count) {
  auto length = get_u32();
  if (!length) {
    return std::unexpected(length.error());
  }
  if (*length > max_count) {
    return make_error(ErrorCode::kResourceLimitExceeded,
                      "length prefix " + std::to_string(*length) + " exceeds maximum " +
                          std::to_string(max_count));
  }
  return *length;
}

Result<std::vector<std::byte>> ByteReader::get_var_bytes(std::uint32_t max_len) {
  auto length = get_length(max_len);
  if (!length) {
    return std::unexpected(length.error());
  }
  return get_bytes(*length);
}

Result<void> ByteReader::expect_end() const {
  if (!at_end()) {
    return make_error(ErrorCode::kParseError,
                      "trailing bytes after value: " + std::to_string(remaining()) + " left");
  }
  return {};
}

}  // namespace blockchain::serialization
