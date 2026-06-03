#ifndef BLOCKCHAIN_TESTS_TESTING_HPP
#define BLOCKCHAIN_TESTS_TESTING_HPP

// Minimal, dependency-free test harness. Kept tiny and deterministic so the
// test suite has no external requirements and builds offline. Each test
// executable links exactly one translation unit's worth of registered cases
// plus test_main.cpp.

#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace bctest {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

struct Failure {
  std::string message;
};

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    registry().push_back(TestCase{std::move(name), std::move(fn)});
  }
};

inline void check(bool condition, const char* expr, const char* file, int line) {
  if (!condition) {
    throw Failure{std::string(file) + ":" + std::to_string(line) + ": CHECK failed: " + expr};
  }
}

inline void fail(const std::string& why, const char* file, int line) {
  throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + why};
}

inline int run_all() {
  std::size_t failed = 0;
  for (const TestCase& test : registry()) {
    try {
      test.fn();
      std::cout << "[ ok ] " << test.name << "\n";
    } catch (const Failure& failure) {
      ++failed;
      std::cout << "[FAIL] " << test.name << "\n       " << failure.message << "\n";
    } catch (const std::exception& ex) {
      ++failed;
      std::cout << "[FAIL] " << test.name << " threw: " << ex.what() << "\n";
    }
  }
  const std::size_t total = registry().size();
  std::cout << (failed == 0 ? "PASSED " : "FAILED ") << (total - failed) << "/" << total << "\n";
  return failed == 0 ? 0 : 1;
}

// Helper: view a string literal/byte string as a span of bytes.
inline std::span<const std::byte> as_bytes(std::string_view text) {
  return std::span<const std::byte>(reinterpret_cast<const std::byte*>(text.data()), text.size());
}

}  // namespace bctest

#define BCTEST_CONCAT2(a, b) a##b
#define BCTEST_CONCAT(a, b) BCTEST_CONCAT2(a, b)

#define TEST_CASE(name)                                                          \
  static void BCTEST_CONCAT(bctest_fn_, __LINE__)();                             \
  static ::bctest::Registrar BCTEST_CONCAT(bctest_reg_, __LINE__){               \
      name, &BCTEST_CONCAT(bctest_fn_, __LINE__)};                              \
  static void BCTEST_CONCAT(bctest_fn_, __LINE__)()

#define CHECK(cond) ::bctest::check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::bctest::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NE(a, b) ::bctest::check((a) != (b), #a " != " #b, __FILE__, __LINE__)
#define FAIL(why) ::bctest::fail((why), __FILE__, __LINE__)

#endif  // BLOCKCHAIN_TESTS_TESTING_HPP
