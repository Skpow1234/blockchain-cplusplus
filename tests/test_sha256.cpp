#include "blockchain/crypto/hash.hpp"
#include "testing.hpp"

using blockchain::crypto::sha256;
using blockchain::crypto::sha256d;
using blockchain::crypto::to_hex;

// Known-answer tests from FIPS 180-4 / standard SHA-256 vectors.

TEST_CASE("sha256 of empty input") {
  CHECK_EQ(to_hex(sha256(bctest::as_bytes(""))),
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("sha256 of 'abc'") {
  CHECK_EQ(to_hex(sha256(bctest::as_bytes("abc"))),
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256 of long message spanning multiple blocks") {
  CHECK_EQ(to_hex(sha256(bctest::as_bytes(
               "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256d equals sha256 of sha256") {
  const auto first = sha256(bctest::as_bytes("blockchain"));
  const auto twice = sha256(std::span<const std::byte>(first.data(), first.size()));
  CHECK_EQ(to_hex(sha256d(bctest::as_bytes("blockchain"))), to_hex(twice));
}

TEST_CASE("sha256 is deterministic") {
  CHECK_EQ(to_hex(sha256(bctest::as_bytes("determinism"))),
           to_hex(sha256(bctest::as_bytes("determinism"))));
}
