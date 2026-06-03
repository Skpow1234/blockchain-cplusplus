# Fuzz targets require a Clang toolchain (-fsanitize=fuzzer). Enable with the
# 'fuzz' preset, which also turns on ASan + UBSan.
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "Fuzz targets require a Clang toolchain (-fsanitize=fuzzer)")
endif()

function(blockchain_add_fuzzer name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE blockchain_core)
  target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
  target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  blockchain_enable_sanitizers(${name})
endfunction()

blockchain_add_fuzzer(fuzz_deserialize_transaction)
blockchain_add_fuzzer(fuzz_deserialize_block)
blockchain_add_fuzzer(fuzz_p2p_message)
blockchain_add_fuzzer(fuzz_p2p_handshake_payload)
blockchain_add_fuzzer(fuzz_decode_ledger)
blockchain_add_fuzzer(fuzz_mempool_admission)
