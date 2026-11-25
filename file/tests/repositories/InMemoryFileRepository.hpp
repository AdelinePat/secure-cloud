#pragma once

#include <algorithm>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "file/repositories/IFileRepository.hpp"

namespace file::repositories::test {

class InMemoryFileRepository : public IFileRepository {
 public:
  domain::FileRecord create(const domain::FileRecord& file) override {
    domain::FileRecord stored = file;
    stored.id = generateId();
    stored.created_at = "1970-01-01T00:00:00Z";
    filesById_[stored.id] = stored;
    return stored;
  }

  std::optional<domain::FileRecord> findById(const std::string& id) override {
    auto it = filesById_.find(id);
    if (it == filesById_.end() || it->second.status == "DELETED")
      return std::nullopt;
    return it->second;
  }

  bool updateStatus(const std::string& id, const std::string& status) override {
    auto it = filesById_.find(id);
    if (it == filesById_.end() || it->second.status == "DELETED") return false;
    it->second.status = status;
    if (status == "AVAILABLE") it->second.completed_at = "1970-01-01T00:00:01Z";
    return true;
  }

  bool softDelete(const std::string& id) override {
    auto it = filesById_.find(id);
    if (it == filesById_.end() || it->second.status == "DELETED") return false;
    it->second.status = "DELETED";
    return true;
  }

  void upsertChunk(const domain::FileChunk& chunk) override {
    chunks_[{chunk.file_id, chunk.index}] = chunk;
  }

  std::vector<domain::FileChunk> listChunks(const std::string& fileId) override {
    std::vector<domain::FileChunk> result;
    for (const auto& [key, chunk] : chunks_) {
      if (key.first == fileId) result.push_back(chunk);
    }
    return result;  // std::map keeps (file_id, index) order
  }

  std::optional<std::string> findParticipantRole(
      const std::string& conversationId, const std::string& userId) override {
    auto it = participants_.find({conversationId, userId});
    if (it == participants_.end()) return std::nullopt;
    return it->second;
  }

  // Test helper: stands in for messaging's conversation_participants rows.
  void addParticipant(const std::string& conversationId,
                      const std::string& userId, const std::string& role) {
    participants_[{conversationId, userId}] = role;
  }

 private:
  std::unordered_map<std::string, domain::FileRecord> filesById_;
  std::map<std::pair<std::string, int32_t>, domain::FileChunk> chunks_;
  std::map<std::pair<std::string, std::string>, std::string> participants_;

  // UUID-shaped, like Postgres' gen_random_uuid().
  static std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << rng()
        << std::setw(16) << rng();
    std::string hex = oss.str();
    return hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" + hex.substr(12, 4) +
           "-" + hex.substr(16, 4) + "-" + hex.substr(20, 12);
  }
};

}  // namespace file::repositories::test
