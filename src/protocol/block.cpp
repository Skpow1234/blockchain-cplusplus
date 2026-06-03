#include "blockchain/protocol/block.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "blockchain/protocol/constants.hpp"

namespace blockchain::protocol {

void BlockHeader::serialize(serialization::ByteWriter& writer) const {
  writer.put_u32(version);
  writer.put_hash(prev_block);
  writer.put_hash(merkle_root);
  writer.put_u64(timestamp);
  writer.put_u32(height);
}

Result<BlockHeader> BlockHeader::deserialize(serialization::ByteReader& reader) {
  BlockHeader header;

  auto version = reader.get_u32();
  if (!version) {
    return std::unexpected(version.error());
  }
  header.version = *version;

  auto prev_block = reader.get_hash();
  if (!prev_block) {
    return std::unexpected(prev_block.error());
  }
  header.prev_block = *prev_block;

  auto merkle_root = reader.get_hash();
  if (!merkle_root) {
    return std::unexpected(merkle_root.error());
  }
  header.merkle_root = *merkle_root;

  auto timestamp = reader.get_u64();
  if (!timestamp) {
    return std::unexpected(timestamp.error());
  }
  header.timestamp = *timestamp;

  auto height = reader.get_u32();
  if (!height) {
    return std::unexpected(height.error());
  }
  header.height = *height;

  return header;
}

crypto::Hash256 BlockHeader::hash() const {
  serialization::ByteWriter writer;
  serialize(writer);
  const std::vector<std::byte>& bytes = writer.data();
  return crypto::sha256d(std::span<const std::byte>(bytes.data(), bytes.size()));
}

void Block::serialize(serialization::ByteWriter& writer) const {
  header.serialize(writer);
  writer.put_u32(static_cast<std::uint32_t>(transactions.size()));
  for (const Transaction& tx : transactions) {
    tx.serialize(writer);
  }
}

Result<Block> Block::deserialize(serialization::ByteReader& reader) {
  Block block;

  auto header = BlockHeader::deserialize(reader);
  if (!header) {
    return std::unexpected(header.error());
  }
  block.header = *header;

  auto tx_count = reader.get_length(kMaxTransactionsPerBlock);
  if (!tx_count) {
    return std::unexpected(tx_count.error());
  }
  block.transactions.reserve(*tx_count);
  for (std::uint32_t i = 0; i < *tx_count; ++i) {
    auto tx = Transaction::deserialize(reader);
    if (!tx) {
      return std::unexpected(tx.error());
    }
    block.transactions.push_back(std::move(*tx));
  }

  return block;
}

std::vector<std::byte> Block::to_bytes() const {
  serialization::ByteWriter writer;
  serialize(writer);
  return writer.data();
}

crypto::Hash256 compute_merkle_root(const std::vector<Transaction>& transactions) {
  if (transactions.empty()) {
    return crypto::zero_hash();
  }

  std::vector<crypto::Hash256> level;
  level.reserve(transactions.size());
  for (const Transaction& tx : transactions) {
    level.push_back(tx.txid());
  }

  while (level.size() > 1) {
    if (level.size() % 2 != 0) {
      level.push_back(level.back());  // duplicate final node on odd levels
    }
    std::vector<crypto::Hash256> next;
    next.reserve(level.size() / 2);
    for (std::size_t i = 0; i < level.size(); i += 2) {
      serialization::ByteWriter writer;
      writer.put_hash(level[i]);
      writer.put_hash(level[i + 1]);
      const std::vector<std::byte>& bytes = writer.data();
      next.push_back(crypto::sha256d(std::span<const std::byte>(bytes.data(), bytes.size())));
    }
    level = std::move(next);
  }

  return level.front();
}

}  // namespace blockchain::protocol
