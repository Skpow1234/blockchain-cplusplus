#ifndef BLOCKCHAIN_ERROR_HPP
#define BLOCKCHAIN_ERROR_HPP

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace blockchain {

// Stable, inspectable error categories shared across the codebase. These are
// intended for tests to assert on, so values must remain meaningful and the
// set must only grow in an additive way.
enum class ErrorCode : std::uint16_t {
  kParseError = 1,
  kInvalidMessage,
  kInvalidTransaction,
  kInvalidBlock,
  kInvalidStateTransition,
  kStorageCorruption,
  kUnsupportedVersion,
  kResourceLimitExceeded,
  kPeerMisbehavior,
  kInvalidConfig,
};

[[nodiscard]] constexpr std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kParseError:
      return "ParseError";
    case ErrorCode::kInvalidMessage:
      return "InvalidMessage";
    case ErrorCode::kInvalidTransaction:
      return "InvalidTransaction";
    case ErrorCode::kInvalidBlock:
      return "InvalidBlock";
    case ErrorCode::kInvalidStateTransition:
      return "InvalidStateTransition";
    case ErrorCode::kStorageCorruption:
      return "StorageCorruption";
    case ErrorCode::kUnsupportedVersion:
      return "UnsupportedVersion";
    case ErrorCode::kResourceLimitExceeded:
      return "ResourceLimitExceeded";
    case ErrorCode::kPeerMisbehavior:
      return "PeerMisbehavior";
    case ErrorCode::kInvalidConfig:
      return "InvalidConfig";
  }
  return "UnknownError";
}

// A structured, inspectable error. The code drives programmatic handling and
// test assertions; the message is for humans and bounded logging.
struct Error {
  ErrorCode code;
  std::string message;

  Error(ErrorCode error_code, std::string detail) : code(error_code), message(std::move(detail)) {}

  explicit Error(ErrorCode error_code)
      : code(error_code), message(std::string(to_string(error_code))) {}

  [[nodiscard]] friend bool operator==(const Error& lhs, const Error& rhs) noexcept {
    return lhs.code == rhs.code;
  }
};

// Project-wide result type. Consensus-adjacent APIs return Result<T> rather
// than throwing across critical boundaries.
template <typename T>
using Result = std::expected<T, Error>;

// Convenience helper to build an unexpected Error result.
[[nodiscard]] inline std::unexpected<Error> make_error(ErrorCode code, std::string detail) {
  return std::unexpected<Error>(Error(code, std::move(detail)));
}

[[nodiscard]] inline std::unexpected<Error> make_error(ErrorCode code) {
  return std::unexpected<Error>(Error(code));
}

}  // namespace blockchain

#endif  // BLOCKCHAIN_ERROR_HPP
