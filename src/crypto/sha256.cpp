#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "blockchain/crypto/hash.hpp"
#include "blockchain/error.hpp"

// Self-contained SHA-256 (FIPS 180-4). Implemented in-tree to avoid external
// dependencies and to guarantee deterministic, portable behavior. This is a
// correctness-first reference implementation; the hot path can be optimized
// later behind the same interface, validated by the known-answer tests.

namespace blockchain::crypto {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) noexcept {
  return (value >> bits) | (value << (32U - bits));
}

class Sha256 {
 public:
  void update(std::span<const std::byte> data) noexcept {
    for (const std::byte byte : data) {
      buffer_[buffer_len_++] = static_cast<std::uint8_t>(byte);
      if (buffer_len_ == kBlockSize) {
        process_block(buffer_.data());
        buffer_len_ = 0;
      }
      ++total_bytes_;
    }
  }

  [[nodiscard]] Hash256 finalize() noexcept {
    const std::uint64_t bit_length = total_bytes_ * 8U;

    // Append the 0x80 padding byte, then zero-pad until 8 bytes remain.
    buffer_[buffer_len_++] = 0x80U;
    if (buffer_len_ > kBlockSize - 8U) {
      while (buffer_len_ < kBlockSize) {
        buffer_[buffer_len_++] = 0U;
      }
      process_block(buffer_.data());
      buffer_len_ = 0;
    }
    while (buffer_len_ < kBlockSize - 8U) {
      buffer_[buffer_len_++] = 0U;
    }

    for (int i = 7; i >= 0; --i) {
      buffer_[buffer_len_++] =
          static_cast<std::uint8_t>((bit_length >> (static_cast<unsigned>(i) * 8U)) & 0xffU);
    }
    process_block(buffer_.data());

    Hash256 digest{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      digest[(i * 4U) + 0] = static_cast<std::byte>((state_[i] >> 24U) & 0xffU);
      digest[(i * 4U) + 1] = static_cast<std::byte>((state_[i] >> 16U) & 0xffU);
      digest[(i * 4U) + 2] = static_cast<std::byte>((state_[i] >> 8U) & 0xffU);
      digest[(i * 4U) + 3] = static_cast<std::byte>(state_[i] & 0xffU);
    }
    return digest;
  }

 private:
  static constexpr std::size_t kBlockSize = 64;

  void process_block(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(block[(i * 4U) + 0]) << 24U) |
             (static_cast<std::uint32_t>(block[(i * 4U) + 1]) << 16U) |
             (static_cast<std::uint32_t>(block[(i * 4U) + 2]) << 8U) |
             (static_cast<std::uint32_t>(block[(i * 4U) + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
      const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3U);
      const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10U);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
      const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + w[i];
      const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_ = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, kBlockSize> buffer_{};
  std::size_t buffer_len_ = 0;
  std::uint64_t total_bytes_ = 0;
};

}  // namespace

Hash256 sha256(std::span<const std::byte> data) {
  Sha256 ctx;
  ctx.update(data);
  return ctx.finalize();
}

Hash256 sha256d(std::span<const std::byte> data) {
  const Hash256 first = sha256(data);
  return sha256(std::span<const std::byte>(first.data(), first.size()));
}

namespace {

[[nodiscard]] bool hex_digit(char c, std::uint8_t& out) {
  if (c >= '0' && c <= '9') {
    out = static_cast<std::uint8_t>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = static_cast<std::uint8_t>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = static_cast<std::uint8_t>(c - 'A' + 10);
    return true;
  }
  return false;
}

}  // namespace

Result<Hash256> hash_from_hex(std::string_view hex) {
  if (hex.size() != 64) {
    return make_error(ErrorCode::kParseError, "hash hex must be exactly 64 characters");
  }
  Hash256 hash{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    std::uint8_t hi = 0;
    std::uint8_t lo = 0;
    if (!hex_digit(hex[i * 2], hi) || !hex_digit(hex[i * 2 + 1], lo)) {
      return make_error(ErrorCode::kParseError, "hash hex contains invalid characters");
    }
    hash[i] = static_cast<std::byte>((hi << 4U) | lo);
  }
  return hash;
}

std::string to_hex(const Hash256& hash) {
  static constexpr std::array<char, 16> kDigits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                                   '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
  std::string out;
  out.reserve(hash.size() * 2);
  for (const std::byte byte : hash) {
    const auto value = static_cast<std::uint8_t>(byte);
    out.push_back(kDigits[value >> 4U]);
    out.push_back(kDigits[value & 0x0fU]);
  }
  return out;
}

}  // namespace blockchain::crypto
