# Core library: consensus-adjacent logic, free of networking and CLI concerns.
add_library(blockchain_core
  crypto/sha256.cpp
  serialization/byte_io.cpp
  protocol/transaction.cpp
  protocol/block.cpp
  state/utxo_set.cpp
  validation/transaction_validation.cpp
  validation/block_validation.cpp
  mempool/mempool.cpp
  mempool/policy.cpp
  production/block_builder.cpp
  consensus/chain.cpp
  storage/chain_store.cpp
  net/p2p_message.cpp
  net/p2p_payloads.cpp
  net/socket_io.cpp
  node/config.cpp
  node/config_json.cpp
  node/genesis.cpp
  node/network.cpp
  node/peer_state.cpp
  node/simulator.cpp)

target_include_directories(blockchain_core
  PUBLIC ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(blockchain_core
  PRIVATE blockchain_warnings)

if(WIN32)
  target_link_libraries(blockchain_core PRIVATE ws2_32)
endif()

blockchain_enable_sanitizers(blockchain_core)

# Node / CLI executable (Stage 1 single-process simulator skeleton).
add_executable(blockchain_node cli/main.cpp)
target_link_libraries(blockchain_node
  PRIVATE blockchain_core blockchain_warnings)
blockchain_enable_sanitizers(blockchain_node)
