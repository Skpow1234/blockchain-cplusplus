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
blockchain_add_test(test_config_json)
blockchain_add_test(test_p2p_message)
blockchain_add_test(test_p2p_payloads)
blockchain_add_test(test_tcp_ping)
blockchain_add_test(test_network_relay)
blockchain_add_test(test_peer_state)
blockchain_add_test(test_storage)
blockchain_add_test(test_adversarial_storage)
blockchain_add_test(test_adversarial_net)
blockchain_add_test(test_restart_integration)
blockchain_add_test(test_simulator)
blockchain_add_test(test_utxo_set)
blockchain_add_test(test_transaction_validation)
blockchain_add_test(test_block_validation)
blockchain_add_test(test_mempool)
blockchain_add_test(test_stage3)
blockchain_add_test(test_block_builder)
blockchain_add_test(test_chain)
blockchain_add_test(test_coinbase)
blockchain_add_test(test_e2e)

# Multi-process network integration (spawns blockchain_node).
blockchain_add_test(test_multinode_process)
target_compile_definitions(test_multinode_process PRIVATE
  "BLOCKCHAIN_NODE_PATH=\"$<TARGET_FILE:blockchain_node>\"")
add_dependencies(test_multinode_process blockchain_node)

# Network tests bind localhost sockets; run serially to avoid cross-test races in CTest.
set(_network_tests
  test_tcp_ping
  test_network_relay
  test_adversarial_net
  test_restart_integration
  test_multinode_process)
foreach(_net_test IN LISTS _network_tests)
  set_tests_properties(${_net_test} PROPERTIES RUN_SERIAL TRUE)
endforeach()
