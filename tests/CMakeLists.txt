# Helper to declare a test executable that links the core library, the shared
# test runner, and registers itself with CTest.
function(blockchain_add_test name)
  add_executable(${name} ${name}.cpp test_main.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE blockchain_core blockchain_warnings)
  blockchain_enable_sanitizers(${name})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

blockchain_add_test(test_sha256)
blockchain_add_test(test_serialization)
blockchain_add_test(test_transaction)
blockchain_add_test(test_block)
blockchain_add_test(test_config)
