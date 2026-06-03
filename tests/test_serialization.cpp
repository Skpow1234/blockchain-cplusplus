#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/error.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "testing.hpp"

using blockchain::ErrorCode;
using blockchain::serialization::ByteReader;
using blockchain::serialization::ByteWriter;

TEST_CASE("fixed-width integers round-trip little-endian") {
  ByteWriter writer;
  writer.put_u8(0x12);
  writer.put_u16(0x3456);
  writer.put_u32(0x789aBCDEU);
  writer.put_u64(0x0123456789abcdefULL);

  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  CHECK_EQ(reader.get_u8().value(), 0x12U);
  CHECK_EQ(reader.get_u16().value(), 0x3456U);
  CHECK_EQ(reader.get_u32().value(), 0x789abcdeU);
  CHECK_EQ(reader.get_u64().value(), 0x0123456789abcdefULL);
  CHECK(reader.expect_end().has_value());
}

TEST_CASE("u32 encodes as little-endian bytes") {
  ByteWriter writer;
  writer.put_u32(1U);
  CHECK_EQ(writer.size(), static_cast<std::size_t>(4));
  CHECK_EQ(static_cast<std::uint8_t>(writer.data()[0]), 0x01U);
  CHECK_EQ(static_cast<std::uint8_t>(writer.data()[1]), 0x00U);
}

TEST_CASE("var bytes round-trip") {
  const std::vector<std::byte> payload = {std::byte{1}, std::byte{2}, std::byte{3}};
  ByteWriter writer;
  writer.put_var_bytes(std::span<const std::byte>(payload.data(), payload.size()));

  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  auto decoded = reader.get_var_bytes(16);
  CHECK(decoded.has_value());
  CHECK(decoded.value() == payload);
}

TEST_CASE("reading past end yields ParseError") {
  ByteWriter writer;
  writer.put_u8(0xAB);
  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  CHECK(reader.get_u32().error().code == ErrorCode::kParseError);
}

TEST_CASE("trailing bytes are rejected") {
  ByteWriter writer;
  writer.put_u8(1);
  writer.put_u8(2);
  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  CHECK(reader.get_u8().has_value());
  CHECK(reader.expect_end().error().code == ErrorCode::kParseError);
}

TEST_CASE("oversized length prefix is rejected before allocation") {
  ByteWriter writer;
  writer.put_u32(1'000'000U);  // claim a huge length
  ByteReader reader(std::span<const std::byte>(writer.data().data(), writer.size()));
  auto result = reader.get_var_bytes(8);
  CHECK(!result.has_value());
  CHECK(result.error().code == ErrorCode::kResourceLimitExceeded);
}
