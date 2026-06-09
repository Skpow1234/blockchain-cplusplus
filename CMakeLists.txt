cmake_minimum_required(VERSION 3.25)

project(blockchain
  VERSION 0.1.0
  DESCRIPTION "Deterministic, testable blockchain simulator in modern C++23"
  LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global standards
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BLOCKCHAIN_BUILD_TESTS "Build unit/integration tests" ON)
option(BLOCKCHAIN_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(BLOCKCHAIN_BUILD_FUZZERS "Build libFuzzer fuzz targets" OFF)

option(BLOCKCHAIN_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(BLOCKCHAIN_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(BLOCKCHAIN_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(BLOCKCHAIN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ---------------------------------------------------------------------------
# Warning flags (applied to our own targets only)
# ---------------------------------------------------------------------------
add_library(blockchain_warnings INTERFACE)
if(MSVC)
  target_compile_options(blockchain_warnings INTERFACE /W4 /permissive-)
  if(BLOCKCHAIN_WARNINGS_AS_ERRORS)
    target_compile_options(blockchain_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(blockchain_warnings INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    -Wold-style-cast -Wcast-align -Wnull-dereference -Wdouble-promotion)
  if(BLOCKCHAIN_WARNINGS_AS_ERRORS)
    target_compile_options(blockchain_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Sanitizer helper. ASan and TSan must never be combined.
# ---------------------------------------------------------------------------
function(blockchain_enable_sanitizers target)
  if(BLOCKCHAIN_ENABLE_ASAN AND BLOCKCHAIN_ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan must not be enabled together")
  endif()

  set(_sanitizers "")
  if(BLOCKCHAIN_ENABLE_ASAN)
    list(APPEND _sanitizers "address")
  endif()
  if(BLOCKCHAIN_ENABLE_UBSAN)
    list(APPEND _sanitizers "undefined")
  endif()
  if(BLOCKCHAIN_ENABLE_TSAN)
    list(APPEND _sanitizers "thread")
  endif()

  if(_sanitizers AND NOT MSVC)
    list(JOIN _sanitizers "," _link_flags)
  # libFuzzer needs coverage instrumentation in every translation unit that ends up
  # in a fuzz binary, but only fuzz executables may link the fuzzer runtime (main).
    set(_compile_flags ${_link_flags})
    if(BLOCKCHAIN_BUILD_FUZZERS)
      set(_compile_flags "${_link_flags},fuzzer-no-link")
    endif()
    target_compile_options(${target} PRIVATE
      -fsanitize=${_compile_flags} -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=${_link_flags})
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library + node binary
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BLOCKCHAIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(BLOCKCHAIN_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ---------------------------------------------------------------------------
# Fuzz targets (require a Clang toolchain with -fsanitize=fuzzer)
# ---------------------------------------------------------------------------
if(BLOCKCHAIN_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
