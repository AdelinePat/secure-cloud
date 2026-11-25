#pragma once

#include <pqxx/pqxx>

#include "file/repositories/IFileRepository.hpp"

namespace file::repositories {

class PostgresFileRepository : public IFileRepository {
 public:
  explicit PostgresFileRepository(pqxx::connection& conn);

  domain::FileRecord create(const domain::FileRecord& file) override;
  std::optional<domain::FileRecord> findById(const std::string& id) override;
  bool updateStatus(const std::string& id, const std::string& status) override;
  bool softDelete(const std::string& id) override;
  void upsertChunk(const domain::FileChunk& chunk) override;
  std::vector<domain::FileChunk> listChunks(const std::string& fileId) override;
  std::optional<std::string> findParticipantRole(
      const std::string& conversationId, const std::string& userId) override;

 private:
  pqxx::connection& conn_;
  static domain::FileRecord rowToFile(const pqxx::row& row);
};

}  // namespace file::repositories
