#include "blockchain/storage/chain_store.hpp"

#include <cerrno>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "blockchain/crypto/hash.hpp"
#include "blockchain/protocol/constants.hpp"
#include "blockchain/serialization/byte_io.hpp"
#include "blockchain/storage/ledger_format.hpp"

namespace blockchain::storage {
namespace {

[[nodiscard]] std::uint32_t ledger_checksum(std::span<const std::byte> body) {
  const crypto::Hash256 digest = crypto::sha256(body);
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < sizeof(value); ++i) {
    value |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(digest[i])) << (8U * i);
  }
  return value;
}

[[nodiscard]] Result<void> put_block(serialization::ByteWriter& writer, const protocol::Block& block) {
  const std::vector<std::byte> bytes = block.to_bytes();
  if (bytes.size() > protocol::kMaxBlockSizeBytes) {
    return make_error(ErrorCode::kResourceLimitExceeded, "block exceeds maximum encoded size");
  }
  writer.put_var_bytes(std::span<const std::byte>(bytes.data(), bytes.size()));
  return {};
}

[[nodiscard]] Result<protocol::Block> get_block(serialization::ByteReader& reader) {
  auto raw = reader.get_var_bytes(protocol::kMaxBlockSizeBytes);
  if (!raw) {
    return std::unexpected(raw.error());
  }
  serialization::ByteReader block_reader(std::span<const std::byte>(raw->data(), raw->size()));
  auto block = protocol::Block::deserialize(block_reader);
  if (!block) {
    return std::unexpected(block.error());
  }
  if (auto end = block_reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }
  return *block;
}

[[nodiscard]] Result<void> validate_block_sequence(std::span<const protocol::Block> blocks) {
  if (blocks.empty()) {
    return make_error(ErrorCode::kStorageCorruption, "ledger contains no blocks");
  }
  if (blocks.front().header.height != 0) {
    return make_error(ErrorCode::kStorageCorruption, "genesis must be at height 0");
  }
  for (std::size_t i = 1; i < blocks.size(); ++i) {
    if (blocks[i].header.height != blocks[i - 1].header.height + 1) {
      return make_error(ErrorCode::kStorageCorruption, "block heights are not contiguous");
    }
  }
  return {};
}

[[nodiscard]] Result<consensus::Chain> replay_chain(std::span<const protocol::Block> blocks,
                                                    const consensus::ConsensusParams& params) {
  if (auto ok = validate_block_sequence(blocks); !ok) {
    return std::unexpected(ok.error());
  }

  auto chain = consensus::Chain::create(blocks.front(), params);
  if (!chain) {
    return std::unexpected(chain.error());
  }
  for (std::size_t i = 1; i < blocks.size(); ++i) {
    if (auto fees = chain->submit_block(blocks[i]); !fees) {
      return std::unexpected(fees.error());
    }
  }
  return *chain;
}

[[nodiscard]] Result<std::vector<std::byte>> read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return make_error(ErrorCode::kStorageCorruption, "ledger file is missing or unreadable");
  }
  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  if (size < 0) {
    return make_error(ErrorCode::kStorageCorruption, "ledger file size is invalid");
  }
  if (static_cast<std::uint64_t>(size) > static_cast<std::uint64_t>(protocol::kMaxBlockSizeBytes) *
                                              static_cast<std::uint64_t>(kMaxLedgerBlocks) +
                                          1024U) {
    return make_error(ErrorCode::kResourceLimitExceeded, "ledger file is too large");
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    return make_error(ErrorCode::kStorageCorruption, "ledger file read failed");
  }
  return bytes;
}

[[nodiscard]] bool is_regular_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  return input.good();
}

[[nodiscard]] Result<void> create_directory(const std::string& path) {
#ifdef _WIN32
  if (_mkdir(path.c_str()) == 0 || errno == EEXIST) {
    return {};
  }
#else
  if (mkdir(path.c_str(), 0755) == 0 || errno == EEXIST) {
    return {};
  }
#endif
  return make_error(ErrorCode::kStorageCorruption,
                    "cannot create data directory: " + std::string(std::strerror(errno)));
}

[[nodiscard]] Result<void> rename_file(const std::string& from, const std::string& to) {
#ifdef _WIN32
  if (MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING) != 0) {
    return {};
  }
  return make_error(ErrorCode::kStorageCorruption,
                    "ledger atomic rename failed (errno=" + std::to_string(GetLastError()) + ")");
#else
  if (rename(from.c_str(), to.c_str()) == 0) {
    return {};
  }
  return make_error(ErrorCode::kStorageCorruption,
                    "ledger atomic rename failed: " + std::string(std::strerror(errno)));
#endif
}

[[nodiscard]] std::string join_path(const std::string& dir, const char* filename) {
  if (dir.empty()) {
    return filename;
  }
  if (dir.back() == '/' || dir.back() == '\\') {
    return dir + filename;
  }
  return dir + "/" + filename;
}

[[nodiscard]] Result<void> write_file_atomic(const std::string& path, const std::string& temp_path,
                                             std::span<const std::byte> bytes) {
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return make_error(ErrorCode::kStorageCorruption, "cannot open ledger temp file for write");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
      return make_error(ErrorCode::kStorageCorruption, "ledger temp file write failed");
    }
  }
  return rename_file(temp_path, path);
}

}  // namespace

ChainStore::ChainStore(std::string data_dir) : data_dir_(std::move(data_dir)) {}

std::string ChainStore::ledger_path(const std::string& data_dir) {
  return join_path(data_dir, ledger_filename());
}

std::string ChainStore::ledger_temp_path(const std::string& data_dir) {
  return join_path(data_dir, ledger_temp_filename());
}

bool ChainStore::ledger_exists() const {
  return is_regular_file(ledger_path(data_dir_));
}

Result<std::vector<std::byte>> ChainStore::encode_ledger(std::span<const protocol::Block> blocks,
                                                       const consensus::ConsensusParams& params) {
  if (blocks.size() > kMaxLedgerBlocks) {
    return make_error(ErrorCode::kResourceLimitExceeded, "too many blocks for ledger");
  }

  serialization::ByteWriter writer;
  writer.put_u32(kLedgerMagic);
  writer.put_u32(kLedgerFormatVersion);
  writer.put_u64(params.block_subsidy);
  writer.put_u32(params.coinbase_maturity);
  writer.put_u32(static_cast<std::uint32_t>(blocks.size()));

  for (const protocol::Block& block : blocks) {
    if (auto ok = put_block(writer, block); !ok) {
      return std::unexpected(ok.error());
    }
  }

  const std::vector<std::byte> body = writer.data();
  const std::uint32_t checksum = ledger_checksum(body);
  serialization::ByteWriter with_checksum;
  with_checksum.put_bytes(std::span<const std::byte>(body.data(), body.size()));
  with_checksum.put_u32(checksum);
  return with_checksum.data();
}

Result<std::pair<std::vector<protocol::Block>, consensus::ConsensusParams>>
ChainStore::decode_ledger(std::span<const std::byte> bytes) {
  if (bytes.size() < sizeof(std::uint32_t)) {
    return make_error(ErrorCode::kStorageCorruption, "ledger file is too short");
  }
  const std::span<const std::byte> body(bytes.data(), bytes.size() - sizeof(std::uint32_t));
  const std::uint32_t expected = ledger_checksum(body);
  std::uint32_t actual = 0;
  for (std::size_t i = 0; i < sizeof(actual); ++i) {
    actual |= static_cast<std::uint32_t>(
                   std::to_integer<unsigned char>(bytes[bytes.size() - sizeof(actual) + i]))
               << (8U * i);
  }
  if (actual != expected) {
    return make_error(ErrorCode::kStorageCorruption, "ledger checksum mismatch");
  }

  serialization::ByteReader reader(body);

  auto magic = reader.get_u32();
  if (!magic || *magic != kLedgerMagic) {
    return make_error(ErrorCode::kStorageCorruption, "ledger magic mismatch");
  }
  auto version = reader.get_u32();
  if (!version || *version != kLedgerFormatVersion) {
    return make_error(ErrorCode::kUnsupportedVersion, "unsupported ledger format version");
  }
  auto subsidy = reader.get_u64();
  if (!subsidy) {
    return std::unexpected(subsidy.error());
  }
  auto maturity = reader.get_u32();
  if (!maturity) {
    return std::unexpected(maturity.error());
  }
  auto count = reader.get_u32();
  if (!count) {
    return std::unexpected(count.error());
  }
  if (*count == 0 || *count > kMaxLedgerBlocks) {
    return make_error(ErrorCode::kStorageCorruption, "ledger block count out of range");
  }

  consensus::ConsensusParams params;
  params.block_subsidy = *subsidy;
  params.coinbase_maturity = *maturity;

  std::vector<protocol::Block> blocks;
  blocks.reserve(*count);
  for (std::uint32_t i = 0; i < *count; ++i) {
    auto block = get_block(reader);
    if (!block) {
      return std::unexpected(block.error());
    }
    blocks.push_back(std::move(*block));
  }

  if (auto end = reader.expect_end(); !end) {
    return std::unexpected(end.error());
  }

  if (auto ok = validate_block_sequence(blocks); !ok) {
    return std::unexpected(ok.error());
  }
  return std::pair{std::move(blocks), params};
}

Result<void> ChainStore::save_ledger(std::span<const protocol::Block> blocks,
                                     const consensus::ConsensusParams& params) {
  if (auto ok = validate_block_sequence(blocks); !ok) {
    return ok;
  }
  auto encoded = encode_ledger(blocks, params);
  if (!encoded) {
    return std::unexpected(encoded.error());
  }

  if (auto dir = create_directory(data_dir_); !dir) {
    return dir;
  }

  return write_file_atomic(ledger_path(data_dir_), ledger_temp_path(data_dir_),
                           std::span<const std::byte>(encoded->data(), encoded->size()));
}

Result<std::pair<std::vector<protocol::Block>, consensus::ConsensusParams>>
ChainStore::load_ledger() {
  auto bytes = read_file(ledger_path(data_dir_));
  if (!bytes) {
    return std::unexpected(bytes.error());
  }
  return decode_ledger(std::span<const std::byte>(bytes->data(), bytes->size()));
}

Result<consensus::Chain> ChainStore::load_chain() {
  auto ledger = load_ledger();
  if (!ledger) {
    return std::unexpected(ledger.error());
  }
  return replay_chain(ledger->first, ledger->second);
}

}  // namespace blockchain::storage
