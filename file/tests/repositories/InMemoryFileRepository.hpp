#pragma once

#include <random>
#include <sstream>
#include <unordered_map>

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
    if (it == filesById_.end()) return false;
    it->second.status = status;
    return true;
  }

  bool softDelete(const std::string& id) override {
    auto it = filesById_.find(id);
    if (it == filesById_.end() || it->second.status == "DELETED") return false;
    it->second.status = "DELETED";
    return true;
  }

 private:
  std::unordered_map<std::string, domain::FileRecord> filesById_;

  static std::string generateId() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream oss;
    oss << std::hex << rng();
    return oss.str();
  }
};

}  // namespace file::repositories::test
