#include "file/repositories/PostgresFileRepository.hpp"

namespace file::repositories {

namespace {

constexpr const char* kFileColumns =
    "id, conversation_id, user_id, status, size_bytes, chunk_count, "
    "encryption_algorithm, created_at::text, completed_at::text";

}  // namespace

PostgresFileRepository::PostgresFileRepository(pqxx::connection& conn)
    : conn_(conn) {}

domain::FileRecord PostgresFileRepository::rowToFile(const pqxx::row& row) {
  domain::FileRecord f;
  f.id = row["id"].as<std::string>();
  f.conversation_id = row["conversation_id"].as<std::string>();
  if (!row["user_id"].is_null()) f.user_id = row["user_id"].as<std::string>();
  f.status = row["status"].as<std::string>();
  f.size_bytes = row["size_bytes"].as<int64_t>();
  f.chunk_count = row["chunk_count"].as<int32_t>();
  f.encryption_algorithm = row["encryption_algorithm"].as<std::string>();
  f.created_at = row["created_at"].as<std::string>();
  if (!row["completed_at"].is_null())
    f.completed_at = row["completed_at"].as<std::string>();
  return f;
}

domain::FileRecord PostgresFileRepository::create(
    const domain::FileRecord& file) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      std::string("INSERT INTO files (conversation_id, user_id, status, "
                  "                   size_bytes, chunk_count, "
                  "                   encryption_algorithm) "
                  "VALUES ($1, $2, $3, $4, $5, $6) RETURNING ") +
          kFileColumns,
      file.conversation_id, file.user_id, file.status, file.size_bytes,
      file.chunk_count, file.encryption_algorithm);

  txn.commit();
  return rowToFile(result[0]);
}

std::optional<domain::FileRecord> PostgresFileRepository::findById(
    const std::string& id) {
  pqxx::work txn(conn_);

  auto result = txn.exec_params(
      std::string("SELECT ") + kFileColumns +
          " FROM files WHERE id = $1 AND deleted_at IS NULL",
      id);

  txn.commit();

  if (result.empty()) return std::nullopt;
  return rowToFile(result[0]);
}

bool PostgresFileRepository::updateStatus(const std::string& id,
                                          const std::string& status) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      // Explicit casts: Postgres cannot deduce one type for $1 from both uses.
      "UPDATE files SET status = $1::varchar, "
      "  completed_at = CASE WHEN $1::varchar = 'AVAILABLE' "
      "                      THEN CURRENT_TIMESTAMP "
      "                      ELSE completed_at END "
      "WHERE id = $2 AND deleted_at IS NULL",
      status, id);
  txn.commit();
  return result.affected_rows() > 0;
}

bool PostgresFileRepository::softDelete(const std::string& id) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "UPDATE files SET status = 'DELETED', deleted_at = CURRENT_TIMESTAMP "
      "WHERE id = $1 AND deleted_at IS NULL",
      id);
  txn.commit();
  return result.affected_rows() > 0;
}

void PostgresFileRepository::upsertChunk(const domain::FileChunk& chunk) {
  pqxx::work txn(conn_);
  txn.exec_params(
      "INSERT INTO file_chunks (file_id, chunk_index, size_bytes, sha256) "
      "VALUES ($1, $2, $3, $4) "
      "ON CONFLICT (file_id, chunk_index) DO UPDATE "
      "SET size_bytes = EXCLUDED.size_bytes, sha256 = EXCLUDED.sha256, "
      "    received_at = CURRENT_TIMESTAMP",
      chunk.file_id, chunk.index, chunk.size_bytes, chunk.sha256);
  txn.commit();
}

std::vector<domain::FileChunk> PostgresFileRepository::listChunks(
    const std::string& fileId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "SELECT chunk_index, size_bytes, sha256 FROM file_chunks "
      "WHERE file_id = $1 ORDER BY chunk_index",
      fileId);
  txn.commit();

  std::vector<domain::FileChunk> chunks;
  chunks.reserve(result.size());
  for (const auto& row : result) {
    chunks.push_back({fileId, row["chunk_index"].as<int32_t>(),
                      row["size_bytes"].as<int32_t>(),
                      row["sha256"].as<std::string>()});
  }
  return chunks;
}

std::optional<std::string> PostgresFileRepository::findParticipantRole(
    const std::string& conversationId, const std::string& userId) {
  pqxx::work txn(conn_);
  auto result = txn.exec_params(
      "SELECT role FROM conversation_participants "
      "WHERE conversation_id = $1 AND user_id = $2",
      conversationId, userId);
  txn.commit();

  if (result.empty()) return std::nullopt;
  return result[0]["role"].as<std::string>();
}

}  // namespace file::repositories
